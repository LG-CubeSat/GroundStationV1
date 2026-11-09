#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../lib/nng/include/nng/nng.h"
#include "../lib/nng/include/nng/protocol/pubsub0/sub.h"
#include "../lib/curl/include/curl/curl.h"
#include "../lib/serial.h"
#include "groundstation_message.h"

#define PACKET_SIZE sizeof(groundstation_telemetry_packet_t) // 20 bytes

#define BACKUP_ADDRESS "telemetry_backup.bin"
#define COPY_ADDRESS "telemetry_copy.bin"

// #define WEB_SERVER_ADDRESS "127.0.0.1:8080"

int write_telemetry(const void *data, size_t len) {
    // save to backup file
    FILE* file = fopen(BACKUP_ADDRESS, "ab");

    if (file == NULL) {
        fprintf(stderr, "Unable to open backup file for writing\n");
        fclose(file);
        return 1;
    }

    size_t bytes_written = fwrite(data, 1, len, file);
    if (bytes_written != len) {
        fprintf(stderr, "Wrote %zu bytes to backup file but expected %zu\n", bytes_written, len);
        fclose(file);
        return 1;
    }

    fclose(file);
    return 0;
}

int pop_telemetry(void *buffer) {
    FILE* file = fopen(BACKUP_ADDRESS, "rb");
    FILE* copy_file = fopen(COPY_ADDRESS, "wb");

    if (file == NULL) {
        fprintf(stderr, "Unable to open backup file for reading\n");
        fclose(file);
        fclose(copy_file);
        return 1;
    }
    if (copy_file == NULL) {
        fprintf(stderr, "Unable to open copy file\n");
        fclose(file);
        fclose(copy_file);
        return 1;
    }

    // read first packet
    size_t bytes_written = fread(buffer, 1, PACKET_SIZE, file);
    if (bytes_written != PACKET_SIZE) {
        fprintf(stderr, "Read %zu bytes from backup file but expected %zu\n", bytes_written, PACKET_SIZE);
        fclose(file);
        fclose(copy_file);
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

    fclose(file);
    fclose(copy_file);

    remove(BACKUP_ADDRESS);
    rename(COPY_ADDRESS, BACKUP_ADDRESS);
    return 0;
}

CURLcode upload_telemetry() {
    CURL *handle;
    CURLcode curl_rv;
    struct curl_slist *headers = NULL;
    long HTTP_rv = 0;
    uint8_t buffer[PACKET_SIZE];

    if (pop_telemetry(buffer) != 0) {
        fprintf(stderr, "Failed to read telemetry from backup file\n");
        return CURLE_READ_ERROR;
    }

    handle = curl_easy_init();
    if (handle) {
        int send = 1;
        curl_rv = curl_easy_setopt(handle, CURLOPT_URL, "https://httpbin.org/post");
        if (curl_rv != CURLE_OK) {
            curl_easy_cleanup(handle);
            curl_slist_free_all(headers);
            return curl_rv;
        }
        headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
        if (headers == NULL) {
            curl_easy_cleanup(handle);
            curl_slist_free_all(headers);
            return curl_rv;
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
            fprintf(stderr, "Server returned HTTP code %ld\n", HTTP_rv);
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(handle);
    } else {
        fprintf(stderr, "Failed to initialize libcurl handle\n");
        return CURLE_FAILED_INIT;
    }
    return CURLE_OK;
}

int main(void)
{
    int receiver_open = 0;
    int nng_rv;
    nng_socket receiver = NNG_SOCKET_INITIALIZER;
    nng_msg *message = NULL;

    int curl_open = 0;
    int packet_count = 0;
    CURLcode curl_rv;

    int result = EXIT_FAILURE;

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
    if ((nng_rv = nng_dial(receiver, GROUNDSTATION_NNG_URL, NULL, 0)) != 0) {
        goto nng_error;
    }

    // set up curl
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        fprintf(stderr, "Unable to initialize libcurl\n");
        goto cleanup;
    }
    curl_open = 1;

    // main loop
    for (;;) {
        // receive and write telemetry to backup file
        if ((nng_rv = nng_recvmsg(receiver, &message, 0)) != 0) {
            goto nng_error;
        }
        if (write_telemetry(nng_msg_body(message), nng_msg_len(message)) == 0) {
            fprintf(stderr, "Telemetry written to backup file\n");
        } else {
            fprintf(stderr, "Failed to write telemetry to backup file\n");
            goto cleanup;
        }
        nng_msg_free(message);
        
        // upload files to server every 50 packets
        packet_count ++;
        if (packet_count >= 50) { // every 50 packets
            curl_rv = upload_telemetry();
            if (curl_rv == CURLE_OK) {
                fprintf(stderr, "Successfully uploaded telemetry data\n");
                packet_count = 0;
            } else {
                fprintf(stderr, "Failed to upload telemetry: %s\n", curl_easy_strerror(curl_rv));
                goto cleanup;
            }
        }
        
        // check if website crash and send backup to server
    }
    result = EXIT_SUCCESS;
nng_error:
    fprintf(stderr, "NNG error: %s\n", nng_strerror(nng_rv));
cleanup:
    if (receiver_open) nng_close(receiver);
    if (curl_open) curl_global_cleanup();
    return result;
}