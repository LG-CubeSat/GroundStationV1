#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <unistd.h>

#include "nng/nng.h"
#include "nng/protocol/pubsub0/sub.h"
#include "curl/curl.h"
#include "serial.h"
#include "groundstation_message.h"

#define PACKET_SIZE sizeof(groundstation_telemetry_packet_t) // 20 bytes
#define MAX_PACKETS_STORED 50

#define BACKUP_ADDRESS "telemetry_backup.bin"
#define COPY_ADDRESS "telemetry_copy.bin"
#define TEMP_ADDRESS "telemetry_temp.bin"

// #define WEB_SERVER_ADDRESS "127.0.0.1:8080"

typedef struct {
    mtx_t file_mutex;
    mtx_t counter_mutex;
    mtx_t stop_mutex;
    long packet_count;
    int stop_thread;
} backup_context_t;

void nng_msg_full_free(nng_msg **message) {
    if (message != NULL && *message != NULL) {
        nng_msg_free(*message);
        *message = NULL;
    }
}

int write_telemetry(const void *data, size_t len) {
    // save to backup file
    FILE* file = fopen(BACKUP_ADDRESS, "ab");

    if (file == NULL) {
        fprintf(stderr, "Unable to open backup file for writing\n");
        return 1;
    }

    size_t bytes_written = fwrite(data, 1, len, file);
    if (bytes_written != len) {
        fprintf(stderr, "Wrote %zu bytes to backup file but expected %zu\n", bytes_written, len);
        fclose(file);
        return 1;
    }

    if (fclose(file) != 0) {
        fprintf(stderr, "Failed to close backup file\n");
        return 1;
    }
    return 0;
}

int read_telemetry(void *buffer) {
    FILE* file = fopen(BACKUP_ADDRESS, "rb");
    if (file == NULL) {
        fprintf(stderr, "Unable to open backup file for reading\n");
        return 1;
    }

    // read first packet
    size_t bytes_written = fread(buffer, 1, PACKET_SIZE, file);
    if (bytes_written != PACKET_SIZE) {
        fprintf(stderr, "Read %zu bytes from backup file but expected %zu\n", bytes_written, PACKET_SIZE);
        fclose(file);
        return 1;
    }
    if (fclose(file) != 0) {
        fprintf(stderr, "Failed to close backup file\n");
        return 1;
    }
    return 0;
}

int free_telemetry() {
    FILE* file = fopen(BACKUP_ADDRESS, "rb");
    if (file == NULL) {
        fprintf(stderr, "Unable to open backup file for reading\n");
        return 1;
    }
    FILE* copy_file = fopen(COPY_ADDRESS, "wb");
    if (copy_file == NULL) {
        fprintf(stderr, "Unable to open copy file\n");
        fclose(file);
        return 1;
    }

    // remove first packet by copying
    if (fseek(file, PACKET_SIZE, SEEK_SET) != 0) {
        fprintf(stderr, "Failed to seek in backup file\n");
        fclose(file);
        fclose(copy_file);
        return 1;
    }
    uint8_t temp_buffer[PACKET_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(temp_buffer, 1, PACKET_SIZE, file)) > 0) {
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

    if (rename(BACKUP_ADDRESS, TEMP_ADDRESS) != 0) {
        fprintf(stderr, "Failed to rename backup file\n");
        return 1;
    }
    if (rename(COPY_ADDRESS, BACKUP_ADDRESS) != 0) {
        fprintf(stderr, "Failed to rename copy file\n");
        if (rename(TEMP_ADDRESS, BACKUP_ADDRESS) != 0) {
            fprintf(stderr, "Failed to restore backup file\n");
        }
        return 1;
    }
    if (remove(TEMP_ADDRESS) != 0) {
        fprintf(stderr, "Failed to remove temporary file\n");
    }
    return 0;
}

CURLcode upload_telemetry(backup_context_t *context) {
    CURL *handle;
    CURLcode curl_rv;
    struct curl_slist *headers = NULL;
    long HTTP_rv = 0;
    uint8_t buffer[PACKET_SIZE];

    mtx_lock(&context->file_mutex);
    if (read_telemetry(buffer) != 0) {
        mtx_unlock(&context->file_mutex);
        fprintf(stderr, "Failed to read telemetry from backup file\n");
        return CURLE_READ_ERROR;
    }
    mtx_unlock(&context->file_mutex);

    handle = curl_easy_init();
    if (handle) {
        curl_rv = curl_easy_setopt(handle, CURLOPT_URL, "https://httpbin.org/post"); // still finding good storage
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
        curl_rv = curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, PACKET_SIZE);
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

long get_packets_saved() {
    long packet_count = 0;
    FILE *file = fopen(BACKUP_ADDRESS, "rb");
    if (file == NULL) {
        FILE *temp_file = fopen(TEMP_ADDRESS, "rb");
        if (temp_file == NULL) {
            fprintf(stderr, "Backup file does not exist, creating new backup file\n");
            if (write_telemetry(NULL, 0) != 0) { // create backup file if doesn't exist
                fprintf(stderr, "Failed to create backup file\n");
                return -1;
            }
            return 0;
        } else {
            fprintf(stderr, "Attempting to recover backup file...\n");
            if (rename(TEMP_ADDRESS, BACKUP_ADDRESS) != 0) {
                fprintf(stderr, "Failed to recover backup file\n");
                return -1;
            }
            fprintf(stderr, "Backup file recovered successfully\n");
            fclose(temp_file);
            file = fopen(BACKUP_ADDRESS, "rb");
            if (file == NULL) {
                fprintf(stderr, "Failed to open recovered backup file\n");
                return -1;
            }
        }
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fprintf(stderr, "Failed to seek in backup file\n");
        fclose(file);
        return -1;
    }
    long size = ftell(file);
    if (size < 0) {
        fprintf(stderr, "Failed to get size of backup file\n");
        fclose(file);
        return -1;
    }
    if (fclose(file) != 0) {
        fprintf(stderr, "Failed to close backup file\n");
        return -1;
    }
    packet_count = size / PACKET_SIZE;
    if (size % PACKET_SIZE != 0) {
        fprintf(stderr, "Backup file contains trailing data\n");
        return -1;
    }
    return packet_count;
}

int upload_thread(void *context_ptr) {
    int curl_open = 0;
    CURLcode curl_rv;

    int result = 1;
    backup_context_t *context = (backup_context_t *)context_ptr;
    int reset_time = 500000; // 0.5 seconds

    // set up curl
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        fprintf(stderr, "Unable to initialize libcurl\n");
        goto cleanup;
    }
    curl_open = 1;

    // main loop
    for (;;) {
        // check if write thread stopped
        mtx_lock(&context->stop_mutex);
        if (context->stop_thread != 0) {
            mtx_unlock(&context->stop_mutex);
            goto cleanup;
        }
        mtx_unlock(&context->stop_mutex);

        mtx_lock(&context->counter_mutex);
        if (context->packet_count < MAX_PACKETS_STORED){
            mtx_unlock(&context->counter_mutex);
            usleep(1000000);
            continue; // if not at capacity, no need to upload
        }
        mtx_unlock(&context->counter_mutex);
        curl_rv = upload_telemetry(context);
        if (curl_rv == CURLE_OK) {
            reset_time = 500000; // reset to 0.5 seconds
            fprintf(stderr, "Successfully uploaded telemetry data\n");
            // remove uploaded packet from backup file
            mtx_lock(&context->file_mutex);
            if (free_telemetry() != 0) {
                mtx_unlock(&context->file_mutex);
                fprintf(stderr, "Failed to free telemetry from backup file\n");
                goto cleanup;
            }
            mtx_unlock(&context->file_mutex);
            mtx_lock(&context->counter_mutex);
            context->packet_count --;
            mtx_unlock(&context->counter_mutex);
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
    mtx_lock(&context->stop_mutex);
    context->stop_thread = 1;
    mtx_unlock(&context->stop_mutex);
    thrd_exit(result);
}

int main(void) {
    int receiver_open = 0;
    int nng_rv;
    nng_socket receiver = NNG_SOCKET_INITIALIZER;
    nng_msg *message = NULL;

    thrd_t upload_thread_id;
    int result = EXIT_FAILURE;
    backup_context_t *context = malloc(sizeof(backup_context_t));
    if (context == NULL) {
        fprintf(stderr, "Failed to allocate memory for backup context\n");
        goto cleanup;
    }
    context->stop_thread = 0;
    int mutex_inits = 0;
    int upload_active = 0;

    // set up NNG
    fprintf(stderr, "Opening NNG receiver socket...\n");
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
    if (mtx_init(&context->file_mutex, mtx_plain) != thrd_success) {
        fprintf(stderr, "Failed to initialize file mutex\n");
        goto cleanup;
    }
    mutex_inits ++;
    if (mtx_init(&context->counter_mutex, mtx_plain) != thrd_success) {
        fprintf(stderr, "Failed to initialize counter mutex\n");
        goto cleanup;
    }
    mutex_inits ++;
    if (mtx_init(&context->stop_mutex, mtx_plain) != thrd_success) {
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
    if (thrd_create(&upload_thread_id, upload_thread, context) != thrd_success) {
        fprintf(stderr, "Failed to create upload thread\n");
        goto cleanup;
    }
    upload_active = 1;

    // main loop
    for (;;) {
        // check if upload thread stopped
        mtx_lock(&context->stop_mutex);
        if (context->stop_thread != 0) {
            mtx_unlock(&context->stop_mutex);
            int thread_result;
            thrd_join(upload_thread_id, &thread_result);
            upload_active = 0;
            goto cleanup;
        }
        mtx_unlock(&context->stop_mutex);

        // check if message on nng channel
        nng_rv = nng_recvmsg(receiver, &message, NNG_FLAG_NONBLOCK);
        if (nng_rv == NNG_EAGAIN) {
            usleep(500000);
            continue;
        }
        if (nng_rv != 0) {
            goto nng_error;
        }

        if (nng_msg_len(message) != PACKET_SIZE) {
            fprintf(stderr, "Dropping packet of size %zu; expected packet of size %zu\n",
                    nng_msg_len(message), PACKET_SIZE);
            nng_msg_full_free(message);
            continue;
        }

        mtx_lock(&context->file_mutex);
        if (write_telemetry(nng_msg_body(message), nng_msg_len(message)) != 0) {
            mtx_unlock(&context->file_mutex);
            fprintf(stderr, "Failed to write telemetry to backup file\n");
            goto cleanup;
        }
        mtx_unlock(&context->file_mutex);
        fprintf(stderr, "Telemetry written to backup file\n");
        mtx_lock(&context->counter_mutex);
        context->packet_count ++;
        mtx_unlock(&context->counter_mutex);
        nng_msg_full_free(message);
    }
    result = EXIT_SUCCESS;
nng_error:
    fprintf(stderr, "NNG error: %s\n", nng_strerror(nng_rv));
cleanup:
    if (context != NULL) {
        if (upload_active == 1) {
            mtx_lock(&context->stop_mutex);
            context->stop_thread = 1;
            mtx_unlock(&context->stop_mutex);
            int thread_result;
            thrd_join(upload_thread_id, &thread_result);
        }
        if (mutex_inits >= 1) mtx_destroy(&context->file_mutex);
        if (mutex_inits >= 2) mtx_destroy(&context->counter_mutex);
        if (mutex_inits >= 3) mtx_destroy(&context->stop_mutex);
    }
    if (message != NULL) nng_msg_full_free(message);
    if (receiver_open) nng_close(receiver);
    return result;
}