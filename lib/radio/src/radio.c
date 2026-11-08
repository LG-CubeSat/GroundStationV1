#include "radio.h"
#include "radio_deframer.h"
#include "radio_queue.h"

#include <pigpiod_if2.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>

/*
real e22-400t30d (433 mhz) over uart in normal (transparent) mode.
m0 + m1 low = normal mode. aux high = idle, aux low = busy.
gpio uses the linux gpio chardev directly, no libgpiod needed.
pins from the obc schematic (pi zero 2 w).
radio_send queues into radio_queue.c, its tx thread frames each chunk in ccsds and calls send_packet.
radio_receive pulls whole ccsds packets out of the uart stream with radio_deframer.c.
*/

#define RADIO_UART_DEVICE "/dev/serial0" // gpio14 txd0 / gpio15 rxd0
#define RADIO_UART_BAUD B9600 // e22 factory default
#define RADIO_GPIO_CHIP "/dev/gpiochip0"
#define RADIO_PIN_M0 24
#define RADIO_PIN_M1 23
#define RADIO_PIN_AUX 25

#define AUX_IDLE_TIMEOUT_MS 5000 // slow air rates take a while to send 240 bytes
#define AUX_BUSY_WAIT_MS 50      // time for aux to drop after we hand it bytes
#define AUX_SETTLE_MS 5          // aux high = buffer empty, not always rf done. small gap before next packet
#define RX_TIMEOUT_MS 1000
#define RX_GAP_MS 20             // uart quiet this long mid packet = packet broke
#define RX_PACKET_MAX_MS 500     // 240 bytes at 9600 baud is ~250ms of extra time once a packet starts
#define INIT_RETRY_MS 1000       // don't hammer a missing module with reinit attempts

/* e22 registers written at every startup so the module always comes up the same */
#define RADIO_CHANNEL 23         // e22-400t30d: 410.125 + channel mhz, 23 = 433.125 mhz
#define RADIO_REG0 0x62          // uart 9600 8n1, air rate 2.4k (factory default)
#define RADIO_REG1 0x00          // 240 byte sub-packet, 30 dbm
#define RADIO_REG3 0x03          // transparent mode, no rssi byte, lbt off
#define CONFIG_SET_TEMP 0xC2     // write registers without saving to flash
#define CONFIG_REPLY 0xC1
#define CONFIG_REPLY_TIMEOUT_MS 1000
#define MODE_SWITCH_MS 2         // e22 wants a short wait after aux goes high on a mode change

/* lock order: init_lock -> tx_lock -> rx_lock */
static pthread_mutex_t init_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t tx_lock = PTHREAD_MUTEX_INITIALIZER; // keeps two senders' packets from interleaving
static pthread_mutex_t rx_lock = PTHREAD_MUTEX_INITIALIZER;
static int uart_fd = -1;
static int mode_fd = -1; // m0 + m1
static int aux_fd = -1;
static atomic_int ready = 0;
static int init_attempted = 0;
static long last_init_ms = 0;
static radio_deframer_t rx_deframer; // holds a partial packet between radio_receive calls

/* 
so the UART on the first OBC is swapped where TX->TX and RX->RX. 
Meaning pins 14/15 need to be switched in SW
Below it makes it so if its zero, keep /dev/serial0 which is normal.
When it is 1, treat them as reversed.
One should note that GPIO14 is normally TX and GPIO15 is normally RX.
*/
#define RADIO_SOFT_RX 14 // Pi receives E22 TXD
#define RADIO_SOFT_TX 15 // Pi sends to E22 RXD
#define RADIO_BAUD_RATE 9600

static int swapped_uart = 0;
static int pigpio_pi = -1;
static int soft_rx_open = 0;

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void sleep_ms(int ms)
{
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static int open_uart(void)
{
    if (swapped_uart) {
        pigpio_pi = pigpio_start(NULL, NULL);
        if (pigpio_pi < 0) return -1;

        if (set_mode(pigpio_pi, RADIO_SOFT_RX, PI_INPUT) < 0 ||
            set_pull_up_down(pigpio_pi, RADIO_SOFT_RX, PI_PUD_UP) < 0 ||
            set_mode(pigpio_pi, RADIO_SOFT_TX, PI_OUTPUT) < 0 ||
            gpio_write(pigpio_pi, RADIO_SOFT_TX, 1) < 0 ||
            bb_serial_read_open(pigpio_pi, RADIO_SOFT_RX, RADIO_BAUD_RATE, 8) < 0) {
            return -1; // radio_init() already calls close all on failures.
        }

        soft_rx_open = 1;
        return 0;
    }

    uart_fd = open(RADIO_UART_DEVICE, O_RDWR | O_NOCTTY);
    if (uart_fd < 0) {
        perror("[RADIO] open uart");
        return -1;
    }

    struct termios tty;
    if (tcgetattr(uart_fd, &tty) != 0) {
        perror("[RADIO] tcgetattr");
        return -1;
    }

    cfmakeraw(&tty); // 8 data bits, no parity, raw bytes
    tty.c_cflag &= ~(CSTOPB | CRTSCTS); // 1 stop bit, no flow control
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;
    cfsetispeed(&tty, RADIO_UART_BAUD);
    cfsetospeed(&tty, RADIO_UART_BAUD);

    if (tcsetattr(uart_fd, TCSANOW, &tty) != 0) {
        perror("[RADIO] tcsetattr");
        return -1;
    }
    tcflush(uart_fd, TCIOFLUSH);
    return 0;
}

static int request_lines(int chip_fd, const unsigned int *pins, unsigned int count, uint64_t flags)
{
    struct gpio_v2_line_request req;
    memset(&req, 0, sizeof(req));

    for (unsigned int i = 0; i < count; i++) req.offsets[i] = pins[i];
    req.num_lines = count;
    req.config.flags = flags;
    strncpy(req.consumer, "e22_radio", sizeof(req.consumer) - 1);

    if (ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0) {
        perror("[RADIO] gpio line request");
        return -1;
    }
    return req.fd;
}

static int read_aux(void)
{
    struct gpio_v2_line_values values = { .bits = 0, .mask = 1 };
    if (ioctl(aux_fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &values) < 0) return -1;
    return (int)(values.bits & 1);
}

static int wait_for_aux(int level, int timeout_ms)
{
    long deadline = now_ms() + timeout_ms;
    for (;;) {
        int aux = read_aux();
        if (aux < 0) return -1;
        if (aux == level) return 0;
        if (now_ms() >= deadline) return -1;
        sleep_ms(1);
    }
}

static int open_gpio(void)
{
    int chip_fd = open(RADIO_GPIO_CHIP, O_RDWR | O_CLOEXEC);
    if (chip_fd < 0) {
        perror("[RADIO] open gpio chip");
        return -1;
    }

    const unsigned int mode_pins[] = { RADIO_PIN_M0, RADIO_PIN_M1 };
    const unsigned int aux_pins[] = { RADIO_PIN_AUX };
    mode_fd = request_lines(chip_fd, mode_pins, 2, GPIO_V2_LINE_FLAG_OUTPUT);
    aux_fd = request_lines(chip_fd, aux_pins, 1, GPIO_V2_LINE_FLAG_INPUT);
    close(chip_fd); // line fds stay valid without the chip fd

    if (mode_fd < 0 || aux_fd < 0) return -1;
    return 0;
}

/* m0 = bit 0, m1 = bit 1, same order the lines were requested in */
static int set_radio_mode(int m0, int m1)
{
    struct gpio_v2_line_values values = {
        .bits = (uint64_t)(m0 ? 1 : 0) | (uint64_t)(m1 ? 2 : 0),
        .mask = 0x3
    };
    if (ioctl(mode_fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &values) < 0) {
        perror("[RADIO] set mode pins");
        return -1;
    }
    if (wait_for_aux(1, AUX_IDLE_TIMEOUT_MS) != 0) return -1;
    sleep_ms(MODE_SWITCH_MS);
    return 0;
}

// returns 1 for a byte, 0 for a timeout, -1 for an error
static int uart_read_byte(uint8_t *byte, int timeout_ms)
{
    long deadline = now_ms() + timeout_ms;

    for (;;) {
        if (swapped_uart) {
            int n = bb_serial_read(pigpio_pi, RADIO_SOFT_RX, byte, 1);
            if (n < 0) return -1;
            if (n == 1) return 1;
            if (now_ms() >= deadline) return 0;
            sleep_ms(1);
            continue;
        }

        int remaining = (int)(deadline - now_ms());
        if (remaining <= 0) return 0;

        struct pollfd pfd = { .fd = uart_fd, .events = POLLIN};
        int ready = poll(&pfd, 1, remaining);
        if (ready < 0) {
            if (errno == EINTR) continue;
            return -1;
        }

        if (ready == 0) return 0;
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) return -1;

        ssize_t n = read(uart_fd, byte, 1);
        if (n == 1) return 1;
        if (n == 0) return -1;
        if (errno != EINTR && errno != EAGAIN) return -1;
    }
}

static int read_exact(uint8_t *buf, size_t length, int timeout_ms)
{
    long deadline = now_ms() + timeout_ms;

    for (size_t got=0; got < length; ++got) {
        int remaining = (int)(deadline - now_ms());
        if (remaining <= 0) return -1;

        if (uart_read_byte(&buf[got], remaining) != 1) return -1;
    }
    return 0;
}

static int write_all(const uint8_t *data, size_t length);

static int uart_flush_input(void)
{
    if (!swapped_uart) return tcflush(uart_fd, TCIFLUSH);

    uint8_t discard[256];
    for (;;) {
        int n = bb_serial_read(pigpio_pi, RADIO_SOFT_RX, discard, sizeof(discard));

        if (n < 0) return -1;
        if (n == 0) return 0;
    }
}

/* config mode is m0 = 0, m1 = 1. the e22 echoes the registers back to confirm */
static int configure_radio(void)
{
    const uint8_t command[] = {
        CONFIG_SET_TEMP, 0x00, 0x09, // write 9 registers starting at 0x00
        0x00, 0x00,                  // module address
        0x00,                        // network id
        RADIO_REG0, RADIO_REG1, RADIO_CHANNEL, RADIO_REG3,
        0x00, 0x00                   // no encryption key
    };
    uint8_t reply[sizeof(command)];

    if (set_radio_mode(0, 1) != 0) {
        fprintf(stderr, "[RADIO] couldn't enter config mode\n");
        return -1;
    }

    int ok = uart_flush_input() == 0 &&
            write_all(command, sizeof(command)) == 0 &&
            read_exact(reply, sizeof(reply), CONFIG_REPLY_TIMEOUT_MS) == 0 &&
            reply[0] == CONFIG_REPLY &&
            memcmp(reply + 1, command + 1, 9) == 0; // address, length, settings. key reads back as 0 anyway
            
    if (!ok) fprintf(stderr, "[RADIO] e22 didn't confirm config\n");

    if (set_radio_mode(0, 0) != 0) {
        fprintf(stderr, "[RADIO] couldn't return to normal mode\n");
        return -1;
    }
    return ok ? 0 : -1;
}

static void close_all(void)
{
    if (uart_fd >= 0) close(uart_fd);
    if (mode_fd >= 0) close(mode_fd);
    if (aux_fd >= 0) close(aux_fd);
    uart_fd = mode_fd = aux_fd = -1;

    if (soft_rx_open) {
        bb_serial_read_close(pigpio_pi, RADIO_SOFT_RX);
        soft_rx_open = 0;
    }
    if (pigpio_pi >= 0) {
        pigpio_stop(pigpio_pi);
        pigpio_pi = -1;
    }
}

static int radio_init(void)
{
    // check if we need to swap the UART. OBC V1 needs this.
    const char *value = getenv("E22_SWAPPED_UART");
    swapped_uart = value && strcmp(value, "1") == 0;

    // configure_radio leaves the module in normal mode on success
    if (open_gpio() != 0 || open_uart() != 0 || configure_radio() != 0) {
        close_all(); // don't leak half-opened fds into the next attempt
        return -1;
    }
    return 0;
}

/* opens the radio if it isn't open. retries after a failure, at most once per INIT_RETRY_MS */
static int ensure_ready(void)
{
    pthread_mutex_lock(&init_lock);
    if (!atomic_load(&ready)) {
        long now = now_ms();
        if (!init_attempted || now - last_init_ms >= INIT_RETRY_MS) {
            init_attempted = 1;
            last_init_ms = now;

            // nobody can be mid send/receive on the old fds while we swap them
            pthread_mutex_lock(&tx_lock);
            pthread_mutex_lock(&rx_lock);
            close_all();
            radio_deframer_reset(&rx_deframer);
            atomic_store(&ready, radio_init() == 0);
            pthread_mutex_unlock(&rx_lock);
            pthread_mutex_unlock(&tx_lock);
        }
    }
    int ok = atomic_load(&ready);
    pthread_mutex_unlock(&init_lock);
    return ok ? 0 : -1;
}

static int write_all(const uint8_t *data, size_t length)
{
    if (swapped_uart) {
        if (length == 0) return 0;

        if (wave_add_new(pigpio_pi) < 0 ||
            wave_add_serial(pigpio_pi, RADIO_SOFT_TX, RADIO_BAUD_RATE,
                            8, 2, 0, (unsigned)length, (char *)data) < 0) {
                return -1;
            }

        int wave = wave_create(pigpio_pi);
        if (wave < 0) return -1;

        int result = wave_send_once(pigpio_pi, wave);
        long deadline = now_ms() + 5000;

        int busy = 0;
        while (result >= 0 && (busy = wave_tx_busy(pigpio_pi)) > 0) {
            if (now_ms() >= deadline) {
                wave_tx_stop(pigpio_pi);
                result = -1;
                break;
            }
            sleep_ms(1);
        }
        if (busy < 0) result = -1;
        if (wave_delete(pigpio_pi, wave) < 0) result = -1;
        return result < 0 ? -1 : 0;
    }

    size_t written = 0;
    while (written < length) {
        ssize_t n = write(uart_fd, data + written, length - written);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            perror("[RADIO] uart write");
            return -1;
        }
        written += (size_t)n;
    }
    return tcdrain(uart_fd); // wait until the pi's uart has actually shifted every byte out
}

/* called by the tx thread in radio_queue.c with one packet (<= 240 bytes) at a time */
static int send_packet(const uint8_t *data, size_t length)
{
    if (ensure_ready() != 0) return -1;

    int result = 0;
    pthread_mutex_lock(&tx_lock);

    if (wait_for_aux(1, AUX_IDLE_TIMEOUT_MS) != 0 || write_all(data, length) != 0) {
        result = -1;
    } else {
        /* aux drops while the e22 transmits. waiting for it to come back
           up keeps each chunk its own packet instead of merging with the next */
        if (wait_for_aux(0, AUX_BUSY_WAIT_MS) != 0) {
            // not fatal: a short packet at a fast air rate can finish between checks
            fprintf(stderr, "[RADIO] aux never went busy after %zu bytes, e22 may not have taken them\n", length);
        }
        if (wait_for_aux(1, AUX_IDLE_TIMEOUT_MS) != 0) {
            fprintf(stderr, "[RADIO] aux stuck busy after send\n");
            result = -1;
        }
        sleep_ms(AUX_SETTLE_MS);
    }

    pthread_mutex_unlock(&tx_lock);
    return result;
}

static int queue(lg_ccsds_content_type_t content, const uint8_t *data, size_t length)
{
    // fail now instead of queueing data the radio can't send
    if (ensure_ready() != 0) return -1;

    return radio_queue_push(content, data, length, send_packet);
}

int radio_send(const uint8_t *data, size_t length)
{
    return queue(LG_CCSDS_CONTENT_RAW_TELEMETRY, data, length);
}

int radio_send_ssdv(const uint8_t *data, size_t length)
{
    return queue(LG_CCSDS_CONTENT_SSDV, data, length);
}

int radio_receive(uint8_t *buffer, uint16_t max_length)
{
    if (buffer == NULL || max_length == 0) return -1;

    if (ensure_ready() != 0) return -1;

    pthread_mutex_lock(&rx_lock);

    uint8_t packet[RADIO_MAX_PACKET_SIZE];
    size_t packet_length = 0;
    long start = now_ms();
    long give_up = start + RX_TIMEOUT_MS + RX_PACKET_MAX_MS; // hard stop even if garbage never ends
    int failed = 0;

    /* read one byte at a time into the deframer. it keeps partial packets between
       calls and uses the ccsds length to know where each packet ends */
    while (packet_length == 0) {
        long now = now_ms();
        if (now >= give_up) break;

        int wait_ms;
        if (radio_deframer_buffered(&rx_deframer) > 0) {
            wait_ms = RX_GAP_MS; // mid packet, the rest should be right behind
        } else {
            wait_ms = (int)(start + RX_TIMEOUT_MS - now);
            if (wait_ms <= 0) break; // timed out, nothing heard
        }

        uint8_t byte;
        int n = uart_read_byte(&byte, wait_ms);
        if (n < 0) {
            failed = 1;
            break;
        }
        if (n == 0) {
            // line went quiet in the middle of a packet.
            radio_stats_rx_skipped(radio_deframer_buffered(&rx_deframer));
            radio_deframer_reset(&rx_deframer);
            continue;
        }
        radio_deframer_push(&rx_deframer, byte);
        packet_length = radio_deframer_next(&rx_deframer, packet);
    }

    pthread_mutex_unlock(&rx_lock);

    if (failed) {
        fprintf(stderr, "[RADIO] uart receive failed, will reopen\n");
        atomic_store(&ready, 0); // reopen on the next call
        return -1;
    }
    if (packet_length == 0) return 0; // timed out, nothing heard

    lg_ccsds_profile_view_t view;
    lg_ccsds_profile_parse(packet, packet_length, &view); // already checked by the deframer
    if (view.payload_length > max_length) return -1;

    memcpy(buffer, view.payload, view.payload_length);
    return (int)view.payload_length;
}
