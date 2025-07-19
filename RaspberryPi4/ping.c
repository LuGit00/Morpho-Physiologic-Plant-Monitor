#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h> // For access()
#include <stdarg.h> // Required for va_start, va_end

#define PING_FILE "/var/www/html/data/ping.txt"
#define DEVICES_FILE "/var/www/html/data/devices.txt"

// Helper function for logging with timestamp
static void log_cgi_message(const char *format, ...) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char timestamp_str[32];
    strftime(timestamp_str, sizeof(timestamp_str), "%Y-%m-%d %H:%M:%S", t);
    fprintf(stderr, "[%s] [CGI Debug] ", timestamp_str);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
}

// Main function for the ping.c CGI script
int main(void) {
    log_cgi_message("ping.c CGI (Refactored from Scratch V2) started."); // Very first log

    char *method = getenv("REQUEST_METHOD");
    if (!method || strcmp(method, "POST") != 0) {
        log_cgi_message("Received non-POST request. This CGI is for POST pings only. Returning 405 Method Not Allowed.");
        puts("Status: 405 Method Not Allowed\nContent-Type: text/plain\n\nMethod Not Allowed. This CGI only accepts POST requests.");
        exit(0);
    }

    char *content_length_str = getenv("CONTENT_LENGTH");
    long content_length = 0;
    if (content_length_str) content_length = strtol(content_length_str, NULL, 10);

    if (content_length <= 0 || content_length > 1024) {
        log_cgi_message("Invalid or too large CONTENT_LENGTH (%ld) for device ping. Returning 400 Bad Request.", content_length);
        puts("Status: 400 Bad Request\nContent-Type: text/plain\n\nInvalid POST data length.");
        exit(0);
    }

    char post_data_buffer[1025]; // Max 1024 bytes + null terminator
    size_t bytes_read = fread(post_data_buffer, 1, content_length, stdin);
    post_data_buffer[bytes_read] = '\0'; // Null-terminate the read data

    char *sender_ip = getenv("REMOTE_ADDR");
    if (!sender_ip) {
        sender_ip = "UNKNOWN_IP";
        log_cgi_message("ERROR: REMOTE_ADDR environment variable not set for device ping.");
    }

    uint64_t device_id_from_raw_ping = 0;
    char *endptr;

    log_cgi_message("Attempting to parse POST body as raw device ID: '%s'", post_data_buffer);
    device_id_from_raw_ping = strtoull(post_data_buffer, &endptr, 10);
    
    // Check if the entire buffer was consumed by the number and it's not empty
    if (endptr == post_data_buffer || *endptr != '\0') { // If parsing failed or extra chars exist
        log_cgi_message("POST body is not a valid raw device ID. Returning 400 Bad Request.");
        puts("Status: 400 Bad Request\nContent-Type: text/plain\n\nInvalid device ping format.");
        exit(0);
    }

    log_cgi_message("Successfully parsed raw device ID: %llu. This is a device ping.", device_id_from_raw_ping);
    
    // --- Append IP to ping.txt ---
    log_cgi_message("Attempting to open ping.txt for appending.");
    FILE *ping_file = fopen(PING_FILE, "a");
    if (ping_file) {
        fprintf(ping_file, "%s\n", sender_ip);
        fclose(ping_file);
        log_cgi_message("Appended %s to ping.txt.", sender_ip);
    } else {
        log_cgi_message("ERROR: Could not open ping.txt for writing (%s).", PING_FILE);
    }

    char response_command[64] = "NO_COMMAND";
    log_cgi_message("Reading devices.txt to find command for device ID %llu.", device_id_from_raw_ping);
    
    // --- Read devices.txt content using fgets line by line ---
    FILE *devices_file_ptr = fopen(DEVICES_FILE, "r");
    if (devices_file_ptr) {
        char line_buffer[512]; // Buffer for reading each line
        int device_found_in_devices_file = 0;

        log_cgi_message("Iterating through devices.txt lines for command lookup.");
        while (fgets(line_buffer, sizeof(line_buffer), devices_file_ptr) != NULL) {
            line_buffer[strcspn(line_buffer, "\n")] = 0; // Remove newline
            
            char *line_copy_for_parsing = strdup(line_buffer); // Duplicate line for strtok_r
            if (!line_copy_for_parsing) {
                log_cgi_message("WARNING: strdup failed for line during command lookup. Skipping line: '%.50s'", line_buffer);
                continue; 
            }

            char *field_token;
            char *saveptr_field;
            uint64_t current_device_id_in_file;

            log_cgi_message("Parsing line: '%.50s'", line_buffer);
            field_token = strtok_r(line_copy_for_parsing, ",", &saveptr_field); // ID
            if (field_token) {
                current_device_id_in_file = strtoull(field_token, NULL, 10);
                log_cgi_message("  Parsed ID: %llu", current_device_id_in_file);

                if (current_device_id_in_file == device_id_from_raw_ping) {
                    device_found_in_devices_file = 1;
                    strtok_r(NULL, ",", &saveptr_field); // IP
                    strtok_r(NULL, ",", &saveptr_field); // Plant ID
                    strtok_r(NULL, ",", &saveptr_field); // Position
                    strtok_r(NULL, ",", &saveptr_field); // Ping Timestamp
                    
                    field_token = strtok_r(NULL, "\n", &saveptr_field); // Command
                    if (field_token) {
                        strncpy(response_command, field_token, sizeof(response_command) - 1);
                        response_command[sizeof(response_command) - 1] = '\0';
                        log_cgi_message("  Found command '%s' for device ID %llu.", response_command, device_id_from_raw_ping);
                    } else {
                        log_cgi_message("  WARNING: Command field missing for device ID %llu. Defaulting to NO_COMMAND.", device_id_from_raw_ping);
                    }
                    free(line_copy_for_parsing);
                    break; // Command found, exit loop
                }
            } else {
                log_cgi_message("WARNING: Could not parse ID from line: '%.50s'", line_buffer);
            }
            free(line_copy_for_parsing);
        }
        fclose(devices_file_ptr);

        if (!device_found_in_devices_file) {
            log_cgi_message("Device ID %llu not found in devices.txt. Returning NO_COMMAND.", device_id_from_raw_ping);
        }
    } else {
        log_cgi_message("ERROR: Could not open devices.txt for reading.");
    }
    // --- End Read devices.txt ---

    log_cgi_message("Sending plain text response: '%s'. Exiting CGI.", response_command);
    puts("Content-Type: text/plain\nStatus: 200 OK\n\n");
    puts(response_command);
    exit(0);
}
