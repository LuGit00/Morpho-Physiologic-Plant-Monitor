#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>

static const char *PING_FILE = "/var/www/html/data/ping.txt";
static const char *DEVICES_FILE = "/var/www/html/data/devices.txt";
static const char *PLANTS_FILE = "/var/www/html/data/plants.txt";
static const char *PROCESSES_FILE = "/var/www/html/data/processes.txt";
static const char *IMAGE_DIR = "/var/www/html/data/images/";

typedef struct { uint64_t id; char *ip; uint8_t plant_id; char *plant_name; uint8_t position; uint64_t ping_timestamp; char *command; uint8_t pinged_this_cycle; } Device;
typedef struct { uint64_t count; Device *list; } Devices;
static Devices devices = {0, NULL};

typedef struct {
    char *name;
    int64_t remaining_duration;
    int64_t configured_duration;
} Plant;
typedef struct { uint64_t count; Plant *list; } Plants;
static Plants plants = {0, NULL};

typedef struct { uint64_t count; char **list; } Pings;
static Pings pings = {0, NULL};

static uint64_t id_generator = 0;

static void log_message(const char *format, ...);
static char *read_file(const char *file_name);
static int write_file(const char *file_name, const char *string_buffer);
static uint64_t generate_new_id(void);
static void free_pings_data(void);
static void free_devices_data(void);
static void free_plants_data(void);
static void process(uint64_t plant_index);

static void read_pings_from_file(void);
static void reset_ping_file(void);
static void read_devices_from_file(void);
static void process_device_pings(void);
static void write_devices_to_file(void);
static void read_plants_from_file(void);
static void manage_global_process_and_plants(void);
static void write_plants_to_file(void);
static void cleanup_all_data(void);

static char* get_first_plant_name() {
    char* content = read_file(PLANTS_FILE);
    if (!content) {
        log_message("WARN: PLANTS_FILE not found or empty when trying to get first plant name.");
        return NULL;
    }

    char* temp_content = strdup(content);
    free(content);
    if (!temp_content) {
        log_message("ERR: strdup failed for temp_content in get_first_plant_name.");
        return NULL;
    }

    char* line;
    char* rest_of_content = temp_content;
    line = strtok_r(rest_of_content, "\n", &rest_of_content);

    if (line) {
        char* name_token;
        char* line_copy_for_strtok = strdup(line);
        if (!line_copy_for_strtok) {
            log_message("ERR: strdup line_copy_for_strtok in get_first_plant_name.");
            free(temp_content);
            return NULL;
        }
        char* inner_rest = line_copy_for_strtok;
        name_token = strtok_r(inner_rest, ",", &inner_rest);

        if (name_token) {
            char* plant_name = strdup(name_token);
            if (!plant_name) {
                log_message("ERR: strdup plant_name in get_first_plant_name.");
            }
            free(line_copy_for_strtok);
            free(temp_content);
            return plant_name;
        }
        free(line_copy_for_strtok);
    }
    
    free(temp_content);
    log_message("WARN: No plant name found in the first line of PLANTS_FILE.");
    return NULL;
}


int main(void) {
    log_message("Application started.");
    while (1) {
        log_message("Loop start.");
        read_pings_from_file();
        reset_ping_file();
        read_devices_from_file();
        process_device_pings();
        write_devices_to_file();
        read_plants_from_file();
        
        if (plants.count > 0) {
            log_message("Triggering immediate image fetching (or placeholder generation) and processing for all plants.");
            for (uint64_t i = 0; i < plants.count; ++i) {
                process(i);
            }
        }

        manage_global_process_and_plants();
        log_message("Loop end. Sleeping for 1 second.");
        sleep(1);
    }
    cleanup_all_data();
    return 0;
}

static void log_message(const char *format, ...) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", t);
    fprintf(stderr, "[%s] ", ts);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
}

static char *read_file(const char *file_name) {
    FILE *file = fopen(file_name, "r");
    if (!file) {
        log_message("ERR: Read %s", file_name);
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *buffer = (char*)malloc(size + 1);
    if (!buffer) {
        log_message("ERR: Malloc for %s", file_name);
        fclose(file);
        return NULL;
    }
    size_t bytes_read = fread(buffer, 1, size, file);
    buffer[bytes_read] = '\0';
    fclose(file);
    return buffer;
}

static int write_file(const char *file_name, const char *string_buffer) {
    FILE *file = fopen(file_name, "w");
    if (!file) {
        log_message("ERR: Write %s", file_name);
        return 1;
    }
    fprintf(file, "%s", string_buffer);
    fclose(file);
    return 0;
}

static uint64_t generate_new_id(void) { return id_generator++; }

static void free_pings_data(void) {
    if (pings.list) {
        for (uint64_t i = 0; i < pings.count; ++i) {
            free(pings.list[i]);
        }
        free(pings.list);
    }
    pings.list = NULL;
    pings.count = 0;
}

static void free_devices_data(void) {
    if (devices.list) {
        for (uint64_t i = 0; i < devices.count; ++i) {
            free(devices.list[i].ip);
            if (devices.list[i].plant_name) free(devices.list[i].plant_name);
            free(devices.list[i].command);
        }
        free(devices.list);
    }
    devices.list = NULL;
    devices.count = 0;
}

static void free_plants_data(void) {
    if (plants.list) {
        for (uint64_t i = 0; i < plants.count; ++i) {
            free(plants.list[i].name);
        }
        free(plants.list);
    }
    plants.list = NULL;
    plants.count = 0;
}

static void process(uint64_t plant_index) {
    log_message("Executing processing for plant index: %llu", plant_index);

    for (uint64_t i = 0; i < devices.count; ++i) {
        if (devices.list[i].plant_id == (plant_index + 1)) {
            char position_char = (char)devices.list[i].position;
            char image_filename[256];
            char full_image_path[512];
            char fetch_command[512];

            snprintf(image_filename, sizeof(image_filename), "plant_%llu_initial_%c.jpg", plant_index + 1, position_char);
            snprintf(full_image_path, sizeof(full_image_path), "%s%s", IMAGE_DIR, image_filename);

            int image_fetched_successfully = 0;

            snprintf(fetch_command, sizeof(fetch_command),
                     "wget -q -O %s http://%s/ --timeout=5 --tries=1",
                     full_image_path, devices.list[i].ip);
            
            log_message("Attempting to fetch image for device %llu (IP: %s, Pos: %c). Command: %s",
                        devices.list[i].id, devices.list[i].ip, position_char, fetch_command);
            
            int ret_fetch = system(fetch_command);
            if (ret_fetch == 0) {
                log_message("Successfully fetched image for device %llu to %s", devices.list[i].id, full_image_path);
                image_fetched_successfully = 1;
            } else {
                log_message("WARN: Failed to fetch image for device %llu. wget exited with status %d. Generating placeholder.", devices.list[i].id, ret_fetch);
            }

            if (!image_fetched_successfully) {
                char placeholder_command[512];
                const char* color = "gray";
                const char* text_color = "black";
                if (position_char == 'X') { color = "lightblue"; text_color = "darkblue"; }
                else if (position_char == 'Y') { color = "lightgreen"; text_color = "darkgreen"; }
                else if (position_char == 'Z') { color = "lightcoral"; text_color = "darkred"; }

                snprintf(placeholder_command, sizeof(placeholder_command),
                         "convert -size 150x100 xc:%s -pointsize 14 -fill %s -gravity Center -annotate 0 'Plant %llu\\nInitial %c' %s",
                         color, text_color, plant_index + 1, position_char, full_image_path);
                
                log_message("Generating placeholder image: %s", placeholder_command);
                int ret_placeholder = system(placeholder_command);
                if (ret_placeholder == 0) {
                    log_message("Successfully generated placeholder image: %s", full_image_path);
                } else {
                    log_message("ERR: Failed to generate placeholder image for %s. convert exited with status %d. Please ensure ImageMagick is installed and in PATH.", full_image_path, ret_placeholder);
                }
            }
        }
    }

    char generate_command[256];
    snprintf(generate_command, sizeof(generate_command), "/usr/local/bin/generate_plant_images %llu", plant_index + 1);
    log_message("Executing generate_plant_images command: %s", generate_command);
    int ret_gen = system(generate_command);
    if (ret_gen == -1) {
        log_message("ERR: Failed to execute generate_plant_images command.");
    } else if (ret_gen != 0) {
        log_message("WARN: generate_plant_images command exited with status %d.", ret_gen);
    } else {
        log_message("generate_plant_images command executed successfully.");
    }
}

static void read_pings_from_file(void) {
    free_pings_data();
    char *content = read_file(PING_FILE);
    if (!content) return;
    char *token, *rest = content;
    while ((token = strtok_r(rest, ",\n", &rest))) {
        int is_unique = 1;
        for (uint64_t i = 0; i < pings.count; ++i) {
            if (strcmp(pings.list[i], token) == 0) {
                is_unique = 0;
                break;
            }
        }
        if (is_unique) {
            pings.list = (char**)realloc(pings.list, (pings.count + 1) * sizeof(char*));
            if (!pings.list) {
                log_message("ERR: Realloc pings");
                free(content);
                return;
            }
            pings.list[pings.count++] = strdup(token);
        }
    }
    free(content);
}

static void reset_ping_file(void) { write_file(PING_FILE, ""); }

static void read_devices_from_file(void) {
    free_devices_data();
    char *content = read_file(DEVICES_FILE);
    if (!content) return;

    char *line, *rest_lines = content;
    while ((line = strtok_r(rest_lines, "\n", &rest_lines))) {
        Device new_dev = {0};
        new_dev.pinged_this_cycle = 0;
        new_dev.plant_name = NULL;

        char *field, *line_copy_for_strtok = strdup(line);
        if (!line_copy_for_strtok) {
            log_message("ERR: strdup line for parsing devices. Skipping line.");
            continue;
        }
        char *current_pos = line_copy_for_strtok;
        char *endptr;

        field = strtok_r(current_pos, ",", &current_pos);
        if (!field) { log_message("ERR: Missing ID in DEVICES_FILE line: %s", line); free(line_copy_for_strtok); continue; }
        new_dev.id = strtoull(field, &endptr, 10);

        field = strtok_r(current_pos, ",", &current_pos);
        if (!field) { log_message("ERR: Missing IP in DEVICES_FILE line: %s", line); free(line_copy_for_strtok); continue; }
        new_dev.ip = strdup(field);
        if (!new_dev.ip) { log_message("ERR: strdup new_dev.ip"); free(line_copy_for_strtok); continue; }

        field = strtok_r(current_pos, ",", &current_pos);
        if (!field) { log_message("ERR: Missing plant_id in DEVICES_FILE line: %s", line); free(new_dev.ip); free(line_copy_for_strtok); continue; }
        new_dev.plant_id = (uint8_t)strtoul(field, &endptr, 10);

        field = strtok_r(current_pos, ",", &current_pos);
        if (!field) {
            log_message("WARN: Missing plant_name in DEVICES_FILE line: %s. Using 'Unassigned'.", line);
            new_dev.plant_name = strdup("Unassigned");
        } else {
            new_dev.plant_name = strdup(field);
        }
        if (!new_dev.plant_name) { log_message("ERR: strdup new_dev.plant_name"); free(new_dev.ip); free(line_copy_for_strtok); continue; }

        field = strtok_r(current_pos, ",", &current_pos);
        if (!field || strlen(field) != 1) { log_message("ERR: Missing or malformed position in DEVICES_FILE line: %s", line); free(new_dev.ip); free(new_dev.plant_name); free(line_copy_for_strtok); continue; }
        new_dev.position = (uint8_t)field[0];

        if (new_dev.position == 'Z') {
            char* first_plant_name = get_first_plant_name();
            if (new_dev.plant_name) {
                free(new_dev.plant_name);
            }
            if (first_plant_name) {
                new_dev.plant_name = first_plant_name;
                log_message("DEBUG: Device %llu at Z position, assigned plant_name to '%s' from PLANTS_FILE.", new_dev.id, new_dev.plant_name);
            } else {
                new_dev.plant_name = strdup("Unassigned");
                log_message("WARN: Device %llu at Z position, could not find first plant name. Assigned 'Unassigned'.", new_dev.id);
            }
            if (!new_dev.plant_name) {
                log_message("CRITICAL ERR: Failed to strdup plant name for Z position.");
                free(new_dev.ip); free(line_copy_for_strtok); continue;
            }
        }


        field = strtok_r(current_pos, ",", &current_pos);
        if (!field) { log_message("ERR: Missing ping_timestamp in DEVICES_FILE line: %s", line); free(new_dev.ip); free(new_dev.plant_name); free(line_copy_for_strtok); continue; }
        new_dev.ping_timestamp = strtoull(field, &endptr, 10);

        if (current_pos && strlen(current_pos) > 0) {
            size_t cmd_len = strlen(current_pos);
            if (cmd_len > 0 && current_pos[cmd_len - 1] == '\n') {
                current_pos[cmd_len - 1] = '\0';
            }
            new_dev.command = strdup(current_pos);
        } else {
            new_dev.command = strdup("NO_COMMAND");
        }
        if (!new_dev.command) { log_message("ERR: strdup new_dev.command"); free(new_dev.ip); free(new_dev.plant_name); free(line_copy_for_strtok); continue; }
        
        devices.list = (Device*)realloc(devices.list, (devices.count + 1) * sizeof(Device));
        if (!devices.list) {
            log_message("ERR: Realloc devices list");
            free(new_dev.ip); free(new_dev.plant_name); free(new_dev.command); free(line_copy_for_strtok);
            free(content);
            devices.count = 0;
            return;
        }
        devices.list[devices.count++] = new_dev;
        if (new_dev.id >= id_generator) id_generator = new_dev.id + 1;
        
        free(line_copy_for_strtok);
    }
    free(content);
}

static void process_device_pings(void) {
    time_t current_time = time(NULL);
    for (uint64_t i = 0; i < devices.count; ++i) devices.list[i].pinged_this_cycle = 0;

    for (uint64_t j = 0; j < pings.count; ++j) {
        char *ping_ip = pings.list[j];
        int found = 0;
        for (uint64_t i = 0; i < devices.count; ++i) {
            if (strcmp(devices.list[i].ip, ping_ip) == 0) {
                devices.list[i].ping_timestamp = (uint64_t)current_time;
                devices.list[i].pinged_this_cycle = 1;
                found = 1;
                break;
            }
        }
        if (!found) {
            Device new_dev;
            new_dev.id = generate_new_id();
            new_dev.ip = strdup(ping_ip);
            if (!new_dev.ip) {
                log_message("ERR: strdup new dev IP");
                continue;
            }
            new_dev.plant_id = 0;
            new_dev.plant_name = strdup("Unassigned");
            if (!new_dev.plant_name) {
                log_message("ERR: strdup new dev plant_name");
                free(new_dev.ip);
                continue;
            }
            new_dev.position = 'U';
            new_dev.ping_timestamp = (uint64_t)current_time;
            new_dev.command = strdup("NO_COMMAND");
            if (!new_dev.command) {
                free(new_dev.ip);
                free(new_dev.plant_name);
                continue;
            }
            new_dev.pinged_this_cycle = 1;
            devices.list = (Device*)realloc(devices.list, (devices.count + 1) * sizeof(Device));
            if (!devices.list) {
                log_message("ERR: Realloc new dev");
                free(new_dev.ip);
                free(new_dev.plant_name);
                free(new_dev.command);
                return;
            }
            devices.list[devices.count++] = new_dev;
        }
    }

    uint64_t i = 0;
    while (i < devices.count) {
        if (devices.list[i].pinged_this_cycle == 0 && (current_time - devices.list[i].ping_timestamp > 60)) {
            free(devices.list[i].ip);
            if (devices.list[i].plant_name) free(devices.list[i].plant_name);
            free(devices.list[i].command);
            for (uint64_t k = i; k < devices.count - 1; ++k) devices.list[k] = devices.list[k + 1];
            devices.count--;
            if (devices.count > 0) devices.list = (Device*)realloc(devices.list, devices.count * sizeof(Device));
            else { free(devices.list); devices.list = NULL; }
        } else { i++; }
    }
}

static void write_devices_to_file(void) {
    if (devices.count == 0) { write_file(DEVICES_FILE, ""); return; }
    size_t buf_sz = 0;
    for (uint64_t i = 0; i < devices.count; ++i) {
        buf_sz += snprintf(NULL, 0, "%llu,%s,%hhu,%s,%c,%llu,%s\n",
                           devices.list[i].id, devices.list[i].ip, devices.list[i].plant_id,
                           devices.list[i].plant_name ? devices.list[i].plant_name : "Unassigned",
                           devices.list[i].position, devices.list[i].ping_timestamp,
                           devices.list[i].command ? devices.list[i].command : "NO_COMMAND");
    }
    char *buffer = (char*)malloc(buf_sz + 1);
    if (!buffer) { log_message("ERR: Malloc dev write"); return; }
    buffer[0] = '\0';
    for (uint64_t i = 0; i < devices.count; ++i) {
        char line[512];
        snprintf(line, sizeof(line), "%llu,%s,%hhu,%s,%c,%llu,%s\n",
                 devices.list[i].id, devices.list[i].ip, devices.list[i].plant_id,
                 devices.list[i].plant_name ? devices.list[i].plant_name : "Unassigned",
                 devices.list[i].position, devices.list[i].ping_timestamp,
                 devices.list[i].command ? devices.list[i].command : "NO_COMMAND");
        strcat(buffer, line);
    }
    write_file(DEVICES_FILE, buffer);
    free(buffer);
}

static void read_plants_from_file(void) {
    free_plants_data();
    char *content = read_file(PLANTS_FILE);
    if (!content) return;
    char *line, *rest_lines = content;
    while ((line = strtok_r(rest_lines, "\n", &rest_lines))) {
        Plant new_plant;
        new_plant.name = strdup("No Name");
        new_plant.remaining_duration = 0;
        new_plant.configured_duration = 3600;

        char *field = strtok(line, ",");
        if (field) { free(new_plant.name); new_plant.name = strdup(field); }
        else { continue; }

        field = strtok(NULL, ",");
        if (field) { new_plant.remaining_duration = strtoll(field, NULL, 10); }

        field = strtok(NULL, ",");
        if (field) { new_plant.configured_duration = strtoll(field, NULL, 10); }
        
        plants.list = (Plant*)realloc(plants.list, (plants.count + 1) * sizeof(Plant));
        if (!plants.list) {
            log_message("ERR: Realloc plants");
            free(new_plant.name);
            free(content);
            return;
        }
        plants.list[plants.count++] = new_plant;
    }
    free(content);
}

static void manage_global_process_and_plants(void) {
    log_message("Managing global process and plant processing.");
    time_t current_time = time(NULL);

    char *processes_content = read_file(PROCESSES_FILE);
    long long global_current_timestamp = 0;
    long long global_set_duration = 3600;

    if (processes_content) {
        char *temp_content = strdup(processes_content);
        if (temp_content) {
            char *ts_curr_str = strtok(temp_content, ",");
            char *ts_set_str = strtok(NULL, "\n");
            if (ts_curr_str) global_current_timestamp = strtoll(ts_curr_str, NULL, 10);
            if (ts_set_str) global_set_duration = strtoll(ts_set_str, NULL, 10);
            free(temp_content);
        } else {
            log_message("ERR: strdup failed for processes_content in manage_global_process_and_plants.");
        }
        free(processes_content);
    } else {
        log_message("WARN: processes.txt not found or unreadable. Initializing default timer.");
        char initial_content[128];
        snprintf(initial_content, sizeof(initial_content), "%lld,%lld\n", (long long)current_time, global_set_duration);
        write_file(PROCESSES_FILE, initial_content);
        log_message("processes.txt initialized with current time. Triggering initial processing.");
        if (plants.count > 0) {
            for (uint64_t i = 0; i < plants.count; ++i) {
                process(i);
            }
        }
        return;
    }

    long long global_time_elapsed = (long long)(current_time - global_current_timestamp);
    long long global_time_remaining = global_set_duration - global_time_elapsed;

    int should_process_plants = 0;

    if (global_current_timestamp == 0) {
        should_process_plants = 1;
        log_message("Global timer is 0. Will start and process plants.");
    } else if (global_time_remaining <= 0) {
        should_process_plants = 1;
        log_message("Global timer expired. Will reset and process plants.");
    } else if (global_time_elapsed >= 0 && global_time_elapsed < 5) {
        should_process_plants = 1;
        log_message("Global timer just started/reset. Will process plants immediately.");
    }

    if (should_process_plants) {
        char new_processes_content[128];
        snprintf(new_processes_content, sizeof(new_processes_content), "%lld,%lld\n", (long long)current_time, global_set_duration);
        write_file(PROCESSES_FILE, new_processes_content);
        log_message("Global timestamp updated to current time for processing cycle.");

        if (plants.count == 0) {
            log_message("No plants defined to process.");
        } else {
            log_message("Iterating through all plants for processing.");
            for (uint64_t i = 0; i < plants.count; ++i) {
                process(i);
            }
            log_message("Finished processing for this cycle.");
        }
    } else {
        log_message("Global process timer is active (%lld seconds remaining). Waiting for expiration.", global_time_remaining);
    }
}

static void write_plants_to_file(void) {
    // This function is now a no-op as application.c should not write to PLANTS_FILE
}

static void cleanup_all_data(void) {
    free_pings_data();
    free_devices_data();
    free_plants_data();
    id_generator = 0;
}
