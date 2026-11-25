#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#include "nng/nng.h"
#include "nng/protocol/pubsub0/sub.h"
#include "curl/curl.h"
#include "groundstation_message.h"
#include "flight_protocol.h"

#define MAX_PACKETS_STORED 50L
#define MAX_PACKET_SIZE sizeof(gs_adcs_telemetry_t)

#define BACKUP_ADDRESS "telemetry_backup.txt"
#define COPY_ADDRESS "telemetry_copy.txt"

static volatile sig_atomic_t keep_running = 0;

typedef struct {
    pthread_mutex_t file_mutex;
    pthread_mutex_t counter_mutex;
    pthread_mutex_t stop_mutex;
    long packet_count;
    int stop_thread;
} backup_context_t;

static void request_shutdown(int signal_number) {
    (void)signal_number;
    keep_running = 0;
}

static void nng_msg_full_free(nng_msg **message) {
    if (message != NULL && *message != NULL) {
        nng_msg_free(*message);
        *message = NULL;
    }
}

static size_t http_callback(char *data, size_t size, size_t count, void *arg) {
    return size * count;
}

// returns size of first packet as well
static long goto_second_packet(FILE* file) {
    int cursor = 0;
    long position;

    while (cursor != (int)';') {
        if (cursor < 0) {
            fprintf(stderr, "Failed to go to end of first packet in backup file\n");
            return -1;
        }
        cursor = fgetc(file);
    }

    position = ftell(file);
    if (position < 0) {
        fprintf(stderr, "Failed to find end of first packet in backup file\n");
        return -1;
    }

    return position;
}

// save to backup file
static int write_telemetry(const uint8_t *data, size_t len) {
    fprintf(stderr, "writing data")
    size_t bytes_written;
    size_t i;
    FILE* file = fopen(BACKUP_ADDRESS, "a");
    if (file == NULL) {
        fprintf(stderr, "Faiiled to open backup file for writing\n");
        return 1;
    }

    for (i = 0; i < len; i++) {
        bytes_written = fprintf(file, "%02X", data[i]);
        if (bytes_written < 2) {
            if (bytes_written < 0) {
                perror("Failed write to backup file");
                fclose(file);
                return 1;
            }
            fprintf(stderr, "Wrote %zu bytes to backup file but expected 2\n", bytes_written);
            fclose(file);
            return 1;
        }
    }
    bytes_written = fprintf(file, ";");
    if (bytes_written < 1) {
        if (bytes_written < 0) {
            perror("Failed write to backup file");
            fclose(file);
            return 1;
        }
        fprintf(stderr, "Wrote %zu bytes to backup file but expected 1\n", bytes_written);
        fclose(file);
        return 1;
    }

    if (fclose(file) != 0) {
        fprintf(stderr, "Failed to close backup file\n");
        return 1;
    }
    return 0;
}

static long read_telemetry(void *buffer) {
    size_t bytes_written;
    long position;
    size_t packet_size;
    FILE* file = fopen(BACKUP_ADDRESS, "r");
    if (file == NULL) {
        fprintf(stderr, "Failed to open backup file for reading\n");
        return -1;
    }

    position = goto_second_packet(file);
    if (position < 0) {
        fclose(file);
        return -1;
    }
    packet_size = (size_t)position;

    if (fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "Failed to reset cursor to beginning of file\n");
        fclose(file);
        return -1;
    }

    // read first packet
    bytes_written = fread(buffer, 1, packet_size, file);
    if (bytes_written != packet_size) {
        fprintf(stderr, "Read %zu bytes from backup file but expected %zu\n", bytes_written, packet_size);
        fclose(file);
        return -1;
    }
    if (fclose(file) != 0) {
        fprintf(stderr, "Failed to close backup file\n");
        return -1;
    }
    return position;
}

static int free_telemetry() {
    size_t packet_size;
    long position;
    FILE* file = fopen(BACKUP_ADDRESS, "r");
    if (file == NULL) {
        fprintf(stderr, "Unable to open backup file for reading\n");
        return 1;
    }
    FILE* copy_file = fopen(COPY_ADDRESS, "w");
    if (copy_file == NULL) {
        fprintf(stderr, "Unable to open copy file\n");
        fclose(file);
        return 1;
    }

    position = goto_second_packet(file);
    if (position < 0) {
        fclose(file);
        fclose(copy_file);
        return 1;
    }
    packet_size = (size_t)position;

    uint8_t temp_buffer[packet_size];
    size_t bytes_read;
    while ((bytes_read = fread(temp_buffer, 1, packet_size, file)) > 0) {
        if (fwrite(temp_buffer, 1, bytes_read, copy_file) != bytes_read) {
            fprintf(stderr, "Failed to write to copy file\n");
            fclose(file);
            fclose(copy_file);
            return 1;
        }
    }

    if (ferror(file) != 0) {
        fprintf(stderr, "Error reading from backup file\n");
        fclose(file);
        fclose(copy_file);
        return 1;
    }

    if (fclose(file) != 0) {
        fprintf(stderr, "Failed to close backup file\n");
        return 1;
    }
    if (fclose(copy_file) != 0) {
        fprintf(stderr, "Failed to close copy file\n");
        return 1;
    }

    if (rename(COPY_ADDRESS, BACKUP_ADDRESS) != 0) {
        fprintf(stderr, "Failed to rename copy file\n");
        return 1;
    }

    return 0;
}

static CURLcode upload_telemetry(backup_context_t *context) {
    CURL *handle;
    CURLcode curl_rv;
    struct curl_slist *headers = NULL;
    long HTTP_rv = 0;
    uint8_t buffer[MAX_PACKET_SIZE];
    long packet_size;

    pthread_mutex_lock(&context->file_mutex);
    packet_size = read_telemetry(buffer);
    if (packet_size < 0) {
        pthread_mutex_unlock(&context->file_mutex);
        fprintf(stderr, "Failed to read telemetry from backup file\n");
        return CURLE_READ_ERROR;
    }
    pthread_mutex_unlock(&context->file_mutex);

    handle = curl_easy_init();
    if (handle) {
        curl_rv = curl_easy_setopt(handle, CURLOPT_URL, "https://httpbin.org/post"); // still finding good storage - look into s3
        if (curl_rv != CURLE_OK) {
            curl_easy_cleanup(handle);
            curl_slist_free_all(headers);
            return curl_rv;
        }
        headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
        if (headers == NULL) {
            curl_easy_cleanup(handle);
            curl_slist_free_all(headers);
            return CURLE_OUT_OF_MEMORY;
        }
        curl_rv = curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
        if (curl_rv != CURLE_OK) {
            curl_easy_cleanup(handle);
            curl_slist_free_all(headers);
            return curl_rv;
        }
        curl_rv = curl_easy_setopt(handle, CURLOPT_POSTFIELDS, buffer);
        if (curl_rv != CURLE_OK) {
            curl_easy_cleanup(handle);
            curl_slist_free_all(headers);
            return curl_rv;
        }
        curl_rv = curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, (size_t)packet_size);
        if (curl_rv != CURLE_OK) {
            curl_easy_cleanup(handle);
            curl_slist_free_all(headers);
            return curl_rv;
        }
        curl_rv = curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, http_callback);
        if (curl_rv != CURLE_OK) {
            curl_easy_cleanup(handle);
            curl_slist_free_all(headers);
            return curl_rv;
        }
        curl_rv = curl_easy_setopt(handle, CURLOPT_TIMEOUT, 5L);
        if (curl_rv != CURLE_OK) {
            curl_easy_cleanup(handle);
            curl_slist_free_all(headers);
            return curl_rv;
        }

        curl_rv = curl_easy_perform(handle);
        if (curl_rv != CURLE_OK) {
            curl_easy_cleanup(handle);
            curl_slist_free_all(headers);
            return curl_rv;
        }
        curl_rv = curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &HTTP_rv);
        if (curl_rv != CURLE_OK) {
            curl_easy_cleanup(handle);
            curl_slist_free_all(headers);
            return curl_rv;
        } else if (HTTP_rv < 200 || HTTP_rv >= 300) {
            fprintf(stderr, "Returned HTTP error code %ld\n", HTTP_rv);
            curl_easy_cleanup(handle);
            curl_slist_free_all(headers);
            return CURLE_HTTP_RETURNED_ERROR;
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(handle);
    } else {
        fprintf(stderr, "Failed to initialize libcurl handle\n");
        return CURLE_FAILED_INIT;
    }
    return CURLE_OK;
}

static long get_packets_saved() {
    long packet_count = 0;
    int prev_char;
    int cursor = 0;
    FILE *file = fopen(BACKUP_ADDRESS, "r");
    if (file == NULL) {
        FILE *copy_file = fopen(COPY_ADDRESS, "r");
        if (copy_file == NULL) {
            fprintf(stderr, "Backup file does not exist, creating new backup file\n");
            if (write_telemetry(NULL, 0) != 0) { // create backup file if doesn't exist
                fprintf(stderr, "Failed to create backup file\n");
                return -1;
            }
            return 0;
        } else {
            fprintf(stderr, "Attempting to recover backup file...\n");
            if (rename(COPY_ADDRESS, BACKUP_ADDRESS) != 0) {
                fprintf(stderr, "Failed to recover backup file\n");
                return -1;
            }
            fprintf(stderr, "Backup file recovered successfully\n");
            fclose(copy_file);
            file = fopen(BACKUP_ADDRESS, "rb");
            if (file == NULL) {
                fprintf(stderr, "Failed to open recovered backup file\n");
                return -1;
            }
        }
    }

    while (cursor >= 0) {
        prev_char = cursor;
        cursor = fgetc(file);
        if (cursor == (int)';') {
            packet_count ++;
        }
    }
    if (cursor != EOF) {
        fprintf(stderr, "Failed to get next character of backup file");
        return -1;
    }

    return packet_count;
}

static void *upload_thread(void *context_ptr) {
    int curl_open = 0;
    CURLcode curl_rv;

    int result = 1;
    backup_context_t *context = (backup_context_t *)context_ptr;
    int reset_time = 500000; // 0.5 seconds

    fprintf(stderr, "Created uploader thread\n");

    // set up curl
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        fprintf(stderr, "Unable to initialize libcurl\n");
        goto cleanup;
    }
    curl_open = 1;

    // main loop
    for(;;) {
        // check if write thread stopped
        pthread_mutex_lock(&context->stop_mutex);
        if (context->stop_thread != 0) {
            pthread_mutex_unlock(&context->stop_mutex);
            goto cleanup;
        }
        pthread_mutex_unlock(&context->stop_mutex);

        pthread_mutex_lock(&context->counter_mutex);
        if (context->packet_count < MAX_PACKETS_STORED){
            pthread_mutex_unlock(&context->counter_mutex);
            usleep(1000000);
            continue; // if not at capacity, no need to upload
        }
        pthread_mutex_unlock(&context->counter_mutex);
        curl_rv = upload_telemetry(context);
        if (curl_rv == CURLE_OK) {
            reset_time = 500000; // reset to 0.5 seconds
            // remove uploaded packet from backup file
            pthread_mutex_lock(&context->file_mutex);
            if (free_telemetry() != 0) {
                pthread_mutex_unlock(&context->file_mutex);
                fprintf(stderr, "Failed to free telemetry from backup file\n");
                goto cleanup;
            }
            pthread_mutex_unlock(&context->file_mutex);
            pthread_mutex_lock(&context->counter_mutex);
            context->packet_count --;
            pthread_mutex_unlock(&context->counter_mutex);
        } else {
            fprintf(stderr, "Failed to upload telemetry, will retry in %d: %s\n", reset_time, curl_easy_strerror(curl_rv));
            usleep(reset_time); // wait for more packets to accumulate
            if (reset_time < 5000000) {
                reset_time += 500000;
            }
        }
    }
    result = 0;
cleanup:
    if (curl_open) curl_global_cleanup();
    pthread_mutex_lock(&context->stop_mutex);
    context->stop_thread = 1;
    pthread_mutex_unlock(&context->stop_mutex);
    return (void *)(intptr_t)result;
}

int main(void) {
    int receiver_open = 0;
    int nng_rv;
    nng_socket receiver = NNG_SOCKET_INITIALIZER;
    nng_msg *message = NULL;

    pthread_t upload_thread_id;
    int result = EXIT_FAILURE;
    backup_context_t *context = malloc(sizeof(backup_context_t));
    if (context == NULL) {
        fprintf(stderr, "Failed to allocate memory for backup context\n");
        goto cleanup;
    }
    context->stop_thread = 0;
    int mutex_inits = 0;
    int upload_active = 0;

    (void)signal(SIGINT, request_shutdown);
    (void)signal(SIGTERM, request_shutdown);
    keep_running = 1;

    // set up NNG
    if ((nng_rv = nng_sub0_open(&receiver)) != 0) {
        goto nng_error;
    }
    receiver_open = 1;
    fprintf(stderr, "NNG receiver socket open\n");
    if ((nng_rv = nng_socket_set(receiver, NNG_OPT_SUB_SUBSCRIBE, "", 0)) != 0) {
        goto nng_error;
    }
    if ((nng_rv = nng_dial(receiver, GROUNDSTATION_NNG_URL, NULL, NNG_FLAG_NONBLOCK)) != 0) {
        goto nng_error;
    }

    // set up mutexes
    if (pthread_mutex_init(&context->file_mutex, NULL) != 0) {
        fprintf(stderr, "Failed to initialize file mutex\n");
        goto cleanup;
    }
    mutex_inits ++;
    if (pthread_mutex_init(&context->counter_mutex, NULL) != 0) {
        fprintf(stderr, "Failed to initialize counter mutex\n");
        goto cleanup;
    }
    mutex_inits ++;
    if (pthread_mutex_init(&context->stop_mutex, NULL) != 0) {
        fprintf(stderr, "Failed to initialize counter mutex\n");
        goto cleanup;
    }
    mutex_inits ++;

    // check backup file - important for first run, but also for checking for trailing data
    context->packet_count = get_packets_saved();
    if (context->packet_count < 0) {
        goto cleanup;
    }

    // separate write data and upload data
    if (pthread_create(&upload_thread_id, NULL, upload_thread, context) != 0) {
        fprintf(stderr, "Failed to create upload thread\n");
        goto cleanup;
    }
    upload_active = 1;

    // main loop
    while (keep_running) {
        // check if upload thread stopped
        pthread_mutex_lock(&context->stop_mutex);
        if (context->stop_thread != 0) {
            pthread_mutex_unlock(&context->stop_mutex);
            void *thread_result;
            pthread_join(upload_thread_id, &thread_result);
            upload_active = 0;
            goto cleanup;
        }
        pthread_mutex_unlock(&context->stop_mutex);

        // check if message on nng channel
        nng_rv = nng_recvmsg(receiver, &message, NNG_FLAG_NONBLOCK);
        if (nng_rv == NNG_EAGAIN) {
            usleep(500000);
            continue;
        }
        if (nng_rv != 0) {
            goto nng_error;
        }

        if (nng_msg_len(message) != sizeof(gs_adcs_telemetry_t) &&
            nng_msg_len(message) != sizeof(gs_command_result_t)) {
            fprintf(stderr, "Dropping packet of unexpected size: %zu\n", nng_msg_len(message));
            nng_msg_full_free(&message);
            continue;
        }

        pthread_mutex_lock(&context->file_mutex);
        if (write_telemetry(nng_msg_body(message), nng_msg_len(message)) != 0) {
            pthread_mutex_unlock(&context->file_mutex);
            fprintf(stderr, "Failed to write telemetry to backup file\n");
            goto cleanup;
        }
        pthread_mutex_unlock(&context->file_mutex);
        pthread_mutex_lock(&context->counter_mutex);
        context->packet_count ++;
        pthread_mutex_unlock(&context->counter_mutex);
        nng_msg_full_free(&message);
    }
    result = EXIT_SUCCESS;
    goto cleanup;
nng_error:
    fprintf(stderr, "NNG error: %s\n", nng_strerror(nng_rv));
cleanup:
    if (context != NULL) {
        if (upload_active == 1) {
            pthread_mutex_lock(&context->stop_mutex);
            context->stop_thread = 1;
            pthread_mutex_unlock(&context->stop_mutex);
            void *thread_result;
            pthread_join(upload_thread_id, &thread_result);
        }
        if (mutex_inits >= 1) pthread_mutex_destroy(&context->file_mutex);
        if (mutex_inits >= 2) pthread_mutex_destroy(&context->counter_mutex);
        if (mutex_inits >= 3) pthread_mutex_destroy(&context->stop_mutex);
    }
    nng_msg_full_free(&message);
    if (receiver_open) nng_close(receiver);
    return result;
}