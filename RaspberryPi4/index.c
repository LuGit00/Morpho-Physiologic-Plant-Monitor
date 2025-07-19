#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>
#include <dirent.h> // For directory listing
#include <sys/stat.h> // For stat() and S_ISREG()

#define PING_FILE "/var/www/html/data/ping.txt"
#define DEVICES_FILE "/var/www/html/data/devices.txt"
#define PLANTS_FILE "/var/www/html/data/plants.txt"
#define PROCESSES_FILE "/var/www/html/data/processes.txt"
#define IMAGE_BASE_DIR "/var/www/html/data/images/" // Define image base directory for index.c

typedef struct { char *name; } plant_lookup_t;
static plant_lookup_t *plant_names_lookup = NULL;
static uint64_t plant_names_count = 0;

// Struct to hold parsed metric data (copied from generate_plant_images.cpp)
typedef struct {
    char timestamp_str[32]; // YYYYMMDD_HHMMSS
    time_t timestamp_t;   // For chronological sorting
    double canopy_area;
    double color_index;
    double height_hp;
    double width1;
    double width2;
    double volumetric_proxy;
} MetricData;


static void log_cgi_message(const char *format, ...) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", t);
    fprintf(stderr, "[%s] [CGI] ", ts);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
}

static char *read_file(const char *f) {
    FILE *fp = fopen(f, "r");
    if (!fp) {
        log_cgi_message("ERR: read %s", f);
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *buf = (char*)malloc(sz + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    size_t br = fread(buf, 1, sz, fp);
    buf[br] = '\0';
    fclose(fp);
    return buf;
}

static int write_file(const char *f, const char *s) {
    FILE *fp = fopen(f, "w");
    if (!fp) {
        log_cgi_message("ERR: write %s", f);
        return 1;
    }
    fprintf(fp, "%s", s);
    fclose(fp);
    return 0;
}

static int plant_name_exists(const char *name) {
    char *content = read_file(PLANTS_FILE);
    if (!content) return 0;
    char *temp = strdup(content);
    free(content);
    if (!temp) return 0;
    char *line;
    char *rest = temp;
    int exists = 0;
    while ((line = strtok_r(rest, "\n", &rest))) {
        char *p_name = strtok(line, ",");
        if (p_name && strcmp(p_name, name) == 0) {
            exists = 1;
            break;
        }
    }
    free(temp);
    return exists;
}

static void load_plant_names_for_lookup() {
    if (plant_names_lookup) {
        for (uint64_t i = 0; i < plant_names_count; ++i) {
            if (plant_names_lookup[i].name) free(plant_names_lookup[i].name);
        }
        free(plant_names_lookup);
        plant_names_lookup = NULL;
        plant_names_count = 0;
    }
    char *content = read_file(PLANTS_FILE);
    if (!content) return;
    char *temp = strdup(content);
    free(content);
    if (!temp) return;
    char *line;
    char *rest = temp;
    uint64_t idx = 0;
    while ((line = strtok_r(rest, "\n", &rest))) {
        char *name_token_ptr;
        char *line_copy_for_strtok_r = strdup(line);
        if (!line_copy_for_strtok_r) {
            log_cgi_message("ERR: strdup line for strtok_r in lookup");
            break;
        }
        char *inner_rest = line_copy_for_strtok_r;
        name_token_ptr = strtok_r(inner_rest, ",", &inner_rest);

        if (!name_token_ptr) {
            free(line_copy_for_strtok_r);
            continue;
        }
        plant_names_lookup = (plant_lookup_t*)realloc(plant_names_lookup, (idx + 1) * sizeof(plant_lookup_t));
        if (!plant_names_lookup) {
            log_cgi_message("ERR: realloc lookup");
            free(line_copy_for_strtok_r);
            break;
        }
        plant_names_lookup[idx].name = strdup(name_token_ptr);
        if (!plant_names_lookup[idx].name) {
            log_cgi_message("ERR: strdup plant name in lookup. Using fallback.");
            plant_names_lookup[idx].name = strdup("ErrorName");
            if (!plant_names_lookup[idx].name) {
                log_cgi_message("CRITICAL ERR: Failed to strdup 'ErrorName' fallback.");
                free(line_copy_for_strtok_r);
                break;
            }
        }
        free(line_copy_for_strtok_r);
        idx++;
    }
    plant_names_count = idx;
    free(temp);
}

static const char* get_plant_name_by_index(uint8_t id) {
    log_cgi_message("DEBUG: get_plant_name_by_index called with id=%u. plant_names_count=%llu", id, plant_names_count);
    if (id > 0 && id <= plant_names_count) {
        log_cgi_message("DEBUG: Returning plant name: %s", plant_names_lookup[id - 1].name);
        return plant_names_lookup[id - 1].name;
    }
    log_cgi_message("DEBUG: Returning 'Unassigned' for id=%u.", id);
    return "Unassigned";
}

static void free_plant_names_lookup() {
    if (plant_names_lookup) {
        for (uint64_t i = 0; i < plant_names_count; ++i) {
            if (plant_names_lookup[i].name) free(plant_names_lookup[i].name);
        }
        free(plant_names_lookup);
        plant_names_lookup = NULL;
        plant_names_count = 0;
    }
}

// Function to parse a single metrics file (copied from generate_plant_images.cpp)
static int parse_metrics_file(const char* filename, MetricData* data) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        log_cgi_message("WARN: Could not open metrics file: %s", filename);
        return 0;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp) != NULL) {
        char key[64], value_str[128];
        if (sscanf(line, "%63[^:]: %127[^\n]", key, value_str) == 2) {
            char *trimmed_key = key;
            while (*trimmed_key == ' ' || *trimmed_key == '\t') trimmed_key++;
            char *end_key = trimmed_key + strlen(trimmed_key) - 1;
            while (end_key > trimmed_key && (*end_key == ' ' || *end_key == '\t')) end_key--;
            *(end_key + 1) = '\0';

            char *trimmed_value = value_str;
            while (*trimmed_value == ' ' || *trimmed_value == '\t') trimmed_value++;
            char *end_value = trimmed_value + strlen(trimmed_value) - 1;
            while (end_value > trimmed_value && (*end_value == ' ' || *end_value == '\t')) end_value--;
            *(end_value + 1) = '\0';

            if (strcmp(trimmed_key, "Timestamp") == 0) {
                strncpy(data->timestamp_str, trimmed_value, sizeof(data->timestamp_str) - 1);
                data->timestamp_str[sizeof(data->timestamp_str) - 1] = '\0';
                struct tm t = {0};
                if (sscanf(data->timestamp_str, "%4d%2d%2d_%2d%2d%2d", 
                           &t.tm_year, &t.tm_mon, &t.tm_mday, 
                           &t.tm_hour, &t.tm_min, &t.tm_sec) == 6) {
                    t.tm_year -= 1900;
                    t.tm_mon -= 1;
                    data->timestamp_t = mktime(&t);
                } else {
                    data->timestamp_t = 0;
                }
            } else if (strcmp(trimmed_key, "Canopy Area (Ac)") == 0) {
                data->canopy_area = atof(trimmed_value);
            } else if (strcmp(trimmed_key, "Color Index (Ihue)") == 0) {
                data->color_index = atof(trimmed_value);
            } else if (strcmp(trimmed_key, "Height (Hp)") == 0) {
                data->height_hp = atof(trimmed_value);
            } else if (strcmp(trimmed_key, "Width 1 (W1)") == 0) {
                data->width1 = atof(trimmed_value);
            } else if (strcmp(trimmed_key, "Width 2 (W2)") == 0) {
                data->width2 = atof(trimmed_value);
            } else if (strcmp(trimmed_key, "Volumetric Proxy (Vp)") == 0) {
                data->volumetric_proxy = atof(trimmed_value);
            }
        }
    }
    fclose(fp);
    return 1;
}

// Function to get the latest metrics data for a given plant ID
static int get_latest_metrics_data(int plant_id, MetricData* latest_data) {
    char plant_id_prefix[64];
    snprintf(plant_id_prefix, sizeof(plant_id_prefix), "plant_%d_metrics_", plant_id);
    size_t prefix_len = strlen(plant_id_prefix);

    DIR *dir;
    struct dirent *ent;
    char latest_filename[256] = {0};
    time_t latest_timestamp = 0;

    if ((dir = opendir(IMAGE_BASE_DIR)) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            // Check if it's a regular file and matches the prefix and suffix
            if (ent->d_type == DT_REG && strncmp(ent->d_name, plant_id_prefix, prefix_len) == 0 &&
                strstr(ent->d_name, ".txt") != NULL) {
                
                char full_path[512];
                snprintf(full_path, sizeof(full_path), "%s%s", IMAGE_BASE_DIR, ent->d_name);

                // Extract timestamp from filename (assuming YYYYMMDD_HHMMSS format after prefix)
                char timestamp_str_from_filename[32] = {0};
                // Ensure we don't read past the end of the string
                size_t filename_len = strlen(ent->d_name);
                if (filename_len >= prefix_len + 15) { // 15 is length of YYYYMMDD_HHMMSS
                    strncpy(timestamp_str_from_filename, ent->d_name + prefix_len, 15);
                    timestamp_str_from_filename[15] = '\0';

                    struct tm t = {0};
                    if (sscanf(timestamp_str_from_filename, "%4d%2d%2d_%2d%2d%2d", 
                               &t.tm_year, &t.tm_mon, &t.tm_mday, 
                               &t.tm_hour, &t.tm_min, &t.tm_sec) == 6) {
                        t.tm_year -= 1900;
                        t.tm_mon -= 1;
                        time_t current_file_timestamp = mktime(&t);

                        if (current_file_timestamp > latest_timestamp) {
                            latest_timestamp = current_file_timestamp;
                            strncpy(latest_filename, full_path, sizeof(latest_filename) - 1);
                            latest_filename[sizeof(latest_filename) - 1] = '\0';
                        }
                    }
                }
            }
        }
        closedir(dir);
    } else {
        log_cgi_message("ERR: Could not open image directory: %s", IMAGE_BASE_DIR);
        return 0;
    }

    if (strlen(latest_filename) > 0) {
        log_cgi_message("INFO: Found latest metrics file: %s", latest_filename);
        return parse_metrics_file(latest_filename, latest_data);
    }
    log_cgi_message("INFO: No metrics file found for plant ID: %d", plant_id);
    return 0;
}


int main(void) {
    load_plant_names_for_lookup();
    log_cgi_message("INFO: Initial plant_names_count after load_plant_names_for_lookup: %llu", plant_names_count);

    char *method = getenv("REQUEST_METHOD");
    char *query_string = getenv("QUERY_STRING");
    int display_detail_plant_idx = -1;

    if (method && strcmp(method, "GET") == 0 && query_string && strlen(query_string) > 0) {
        char *qs_copy = strdup(query_string);
        char *param_tok, *param_rest = qs_copy;
        while ((param_tok = strtok_r(param_rest, "&", &param_rest))) {
            char *key = param_tok;
            char *val = strchr(param_tok, '=');
            if (val) {
                *val++ = '\0';
                if (strcmp(key, "plant_detail_idx") == 0) {
                    display_detail_plant_idx = atoi(val);
                    break;
                }
            }
        }
        free(qs_copy);
    }

    if (method && strcmp(method, "POST") == 0) {
        long len = strtol(getenv("CONTENT_LENGTH"), NULL, 10);
        if (len <= 0 || len > 1024) { puts("Status: 400 Bad Request\nContent-Type: text/plain\n\nInvalid POST data."); exit(0); }
        char post_data[1025];
        fread(post_data, 1, len, stdin);
        post_data[len] = '\0';
        char *dev_id_str = "", *action = "", *plant_name = "", *minutes_str = "", *plant_idx_str = "";
        char *tok, *rest = post_data;
        while ((tok = strtok_r(rest, "&", &rest))) {
            char *key = tok, *val = strchr(tok, '=');
            if (val) { *val++ = '\0'; for (char *p = val; *p; p++) if (*p == '+') *p = ' '; } else val = "";
            if (strcmp(key, "device_id") == 0) dev_id_str = val;
            else if (strcmp(key, "action") == 0) action = val;
            else if (strcmp(key, "plantName") == 0) plant_name = val;
            else if (strcmp(key, "minutes") == 0) minutes_str = val;
            else if (strcmp(key, "plant_index") == 0) plant_idx_str = val;
        }

        if (strcmp(action, "add_plant") == 0 && strlen(plant_name) > 0) {
            if (!plant_name_exists(plant_name)) { FILE *fp = fopen(PLANTS_FILE, "a"); if (fp) { fprintf(fp, "%s,%lld,%lld\n", plant_name, (long long)0, (long long)3600); fclose(fp); } }
        } else if (strncmp(action, "assign_device_", 14) == 0 && strlen(dev_id_str) > 0) {
            load_plant_names_for_lookup(); 
            log_cgi_message("INFO: Re-loaded plant_names_lookup before assignment. Current plant_names_count: %llu", plant_names_count);

            uint64_t dev_id = strtoull(dev_id_str, NULL, 10);
            char pos_char = action[14];
            int p_idx = atoi(plant_idx_str);
            char *dev_content = read_file(DEVICES_FILE);
            if (dev_content) {
                char new_dev_content[8192] = "";
                char *line, *r_line = dev_content;
                int found = 0;
                log_cgi_message("INFO: Processing assign_device_ POST. Current plant_names_count: %llu", plant_names_count);

                while((line = strtok_r(r_line, "\n", &r_line))) {
                    char *id_s, *ip_s, *p_id_s, *current_plant_name_s, *pos_s, *ts_s, *cmd_s;
                    char *t_line = strdup(line);
                    
                    id_s = strtok(t_line, ",");
                    ip_s = strtok(NULL, ",");
                    p_id_s = strtok(NULL, ",");
                    current_plant_name_s = strtok(NULL, ",");
                    pos_s = strtok(NULL, ",");
                    ts_s = strtok(NULL, ",");
                    cmd_s = strtok(NULL, "\n");

                    if (id_s && ip_s && p_id_s && current_plant_name_s && pos_s && ts_s && cmd_s) {
                        uint64_t cur_id = strtoull(id_s, NULL, 10);
                        uint8_t cur_p_id = (uint8_t)strtoul(p_id_s, NULL, 10);
                        char cur_pos = pos_s[0];

                        if (cur_id == dev_id) {
                            const char* assigned_plant_name = get_plant_name_by_index(p_idx + 1);
                            log_cgi_message("DEBUG: Assigning device %llu to plant index %d. Name used: %s", cur_id, p_idx + 1, assigned_plant_name);
                            sprintf(new_dev_content + strlen(new_dev_content), "%llu,%s,%d,%s,%c,%s,%s\n",
                                    cur_id, ip_s, p_idx + 1, assigned_plant_name, pos_char, ts_s, cmd_s);
                            found = 1;
                        }
                        else if (cur_p_id == (p_idx + 1) && cur_pos == pos_char) {
                            sprintf(new_dev_content + strlen(new_dev_content), "%llu,%s,%d,%s,%c,%s,%s\n",
                                    cur_id, ip_s, 0, "Unassigned", 'U', ts_s, "NO_COMMAND");
                        }
                        else {
                            sprintf(new_dev_content + strlen(new_dev_content), "%llu,%s,%d,%s,%c,%s,%s\n",
                                    cur_id, ip_s, cur_p_id, current_plant_name_s, cur_pos, ts_s, cmd_s);
                        }
                    } else {
                        log_cgi_message("WARN: Malformed line in DEVICES_FILE during assignment: %s", line);
                        sprintf(new_dev_content + strlen(new_dev_content), "%s\n", line);
                    }
                    free(t_line);
                }
                if (!found) log_cgi_message("WARN: Device ID %s not found for assignment", dev_id_str);
                write_file(DEVICES_FILE, new_dev_content);
                free(dev_content);
            }
        } else if (strcmp(action, "set_minutes") == 0 || strcmp(action, "start_all_processes") == 0 || strcmp(action, "reset_all_processes") == 0) {
            char *processes_content = read_file(PROCESSES_FILE);
            long long current_timestamp = 0;
            long long timestamp_set = 3600;

            if (processes_content) {
                char *temp_content = strdup(processes_content);
                char *ts_curr_str = strtok(temp_content, ",");
                char *ts_set_str = strtok(NULL, "\n");
                if (ts_curr_str) current_timestamp = strtoll(ts_curr_str, NULL, 10);
                if (ts_set_str) timestamp_set = strtoll(ts_set_str, NULL, 10);
                free(temp_content);
                free(processes_content);
            }

            if (strcmp(action, "set_minutes") == 0) {
                timestamp_set = strtoll(minutes_str, NULL, 10) * 60;
                log_cgi_message("Set global process duration to %lld seconds.", timestamp_set);
            } else if (strcmp(action, "start_all_processes") == 0) {
                current_timestamp = time(NULL);
                log_cgi_message("Started all processes. Global timer reset to current time.");
            } else if (strcmp(action, "reset_all_processes") == 0) {
                current_timestamp = 0;
                log_cgi_message("Reset all processes. Global timer set to 0.");
            }

            char new_processes_content[128];
            snprintf(new_processes_content, sizeof(new_processes_content), "%lld,%lld\n", current_timestamp, timestamp_set);
            write_file(PROCESSES_FILE, new_processes_content);

        } else if (strcmp(action, "get_result") == 0) {
            puts("Content-Type: text/plain\nContent-Disposition: attachment; filename=\"devices_data.txt\"\nStatus: 200 OK\n\n");
            char *content = read_file(DEVICES_FILE);
            if (content) { puts(content); free(content); } else { puts("Error: No data."); }
            exit(0);
        }
        load_plant_names_for_lookup();
        puts("Status: 302 Found\nLocation: /cgi-bin/index.cgi\n\n");
        exit(0);
    } else {
        time_t current_cgi_time = time(NULL);

        char *processes_content = read_file(PROCESSES_FILE);
        long long global_current_timestamp = 0;
        long long global_set_duration = 3600;
        char global_timer_status_str[128];

        if (processes_content) {
            char *temp_content = strdup(processes_content);
            if (temp_content) {
                char *ts_curr_str = strtok(temp_content, ",");
                char *ts_set_str = strtok(NULL, "\n");
                if (ts_curr_str) global_current_timestamp = strtoll(ts_curr_str, NULL, 10);
                if (ts_set_str) global_set_duration = strtoll(ts_set_str, NULL, 10);
                free(temp_content);
            } else {
                log_cgi_message("ERR: strdup failed for processes_content in GET request.");
            }
            free(processes_content);
        }

        long long time_remaining = 0;
        if (global_current_timestamp > 0) {
            time_remaining = (global_current_timestamp + global_set_duration) - current_cgi_time;
            if (time_remaining < 0) time_remaining = 0;
        }

        if (global_current_timestamp == 0) {
            strcpy(global_timer_status_str, "Not Started / Reset");
        } else if (time_remaining == 0) {
            strcpy(global_timer_status_str, "Completed. Click Start All to rerun.");
        } else {
            long long minutes_rem = time_remaining / 60;
            long long seconds_rem = time_remaining % 60;
            snprintf(global_timer_status_str, sizeof(global_timer_status_str), "%lld min %lld sec remaining", minutes_rem, seconds_rem);
        }

        long long initial_minutes_value = global_set_duration / 60;
        if (initial_minutes_value == 0 && global_set_duration > 0) {
            initial_minutes_value = 1;
        } else if (global_set_duration == 0) {
            initial_minutes_value = 60;
        }


        puts("Content-Type: text/html\n\n"
             "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
             "<title>Morpho-Physiologic Plant Monitor</title><style>"
             "body{font-family:Arial,sans-serif;background-color:#f0f0f0;color:#333;margin:20px;padding:0;display:flex;flex-direction:column;align-items:center;justify-content:flex-start;min-height:90vh;}"
             "h1{color:#0056b3;text-align:center;border-bottom:2px solid #0056b3;padding-bottom:10px;margin-bottom:20px;width:80%;max-width:800px;}"
             "h2{color:#007bff;text-align:center;margin-top:30px;margin-bottom:15px;width:80%;max-width:800px;}"
             ".container{background-color:#ffffff;padding:30px;border-radius:10px;box-shadow:0 4px 8px rgba(0,0,0,0.1);margin-bottom:20px;width:90%;max-width:900px;box-sizing:border-box;}"
             "table{width:100%;border-collapse:collapse;margin-top:15px;}"
             "th,td{border:1px solid #ddd;padding:8px;text-align:left;}"
             "th{background-color:#f2f2f2;color:#555;}"
             ".button-group{display:flex;gap:10px;margin-top:10px;justify-content:center;flex-wrap:wrap;}"
             "button,input[type=\"submit\"]{background-color:#007bff;color:white;padding:8px 15px;border:none;border-radius:5px;cursor:pointer;font-size:1em;transition:background-color 0.3s ease;box-shadow:0 2px 4px rgba(0,0,0,0.1);}"
             "button:hover,input[type=\"submit\"]:hover{background-color:#0056b3;}"
             "input[type=\"text\"],input[type=\"number\"]{padding:8px;border:1px solid #ccc;border-radius:5px;margin-right:5px;width:80px;}"
             ".graph-placeholder{width:150px;height:50px;background-color:#e9ecef;border:1px dashed #adb5bd;display:flex;align-items:center;justify-content:center;font-size:0.8em;color:#6c757d;border-radius:5px;}"
             ".assign-device-cell { display: flex; flex-wrap: wrap; align-items: center; gap: 10px; }"
             ".assign-device-row { display: flex; align-items: center; gap: 5px; }"
             ".plant-panel { margin-top: 10px; }"
             ".plant-panel h3 { color:#007bff; margin-top: 20px; margin-bottom: 10px; text-align: center; font-size: 1.1em;}"
             ".plant-panel table { width: 100%; border-collapse: collapse; margin-top: 10px; background-color: #ffffff; box-shadow:0 2px 4px rgba(0,0,0,0.05); border-radius: 8px; overflow: hidden; }"
             ".plant-panel th, .plant-panel td { padding: 10px; text-align: center; border: 1px solid #f0f0f0; }"
             ".plant-panel th { background-color: #fafafa; color: #666; font-weight: bold; }"
             ".plant-panel td img { max_width: 150px; height: auto; display: block; margin: 0 auto; border: none; border-radius: 4px; }"
             ".plant-panel tr:nth-child(even) { background-color: #fcfcfc; }"
             "</style></head><body><h1>Morpho-Physiologic Plant Monitor</h1><div class=\"container\"><h2>Connected Devices</h2><table><thead><tr><th>Index</th><th>IP</th><th>Plant Name</th><th>Position</th><th>Last Ping</th><th>Command</th><th>Live Image</th></tr></thead><tbody>");

        char *dev_content = read_file(DEVICES_FILE);
        if (dev_content) {
            char *line, *r_line = dev_content;
            while ( (line = strtok_r(r_line, "\n", &r_line)) ) {
                char *id_s, *ip_s, *p_id_s, *plant_name_s, *pos_s, *ts_s, *cmd_s;
                char *t_line = strdup(line);
                
                id_s = strtok(t_line, ",");
                ip_s = strtok(NULL, ",");
                p_id_s = strtok(NULL, ",");
                plant_name_s = strtok(NULL, ",");
                pos_s = strtok(NULL, ",");
                ts_s = strtok(NULL, ",");
                cmd_s = strtok(NULL, "\n");

                if (id_s && ip_s && p_id_s && plant_name_s && pos_s && ts_s && cmd_s) {
                    uint64_t id = strtoull(id_s, NULL, 10);
                    uint8_t p_id = (uint8_t)strtoul(p_id_s, NULL, 10);
                    char pos = pos_s[0];
                    uint64_t ts = strtoull(ts_s, NULL, 10);
                    char ts_str[64];
                    strftime(ts_str, sizeof(ts_str), "%Y-%m-%d %H:%M:%S", localtime((time_t*)&ts));
                    printf("<tr><td>%llu</td><td>%s</td><td>%s</td><td>%c</td><td>%s</td><td>%s</td><td><img src=\"http://%s/\" width=\"100\" height=\"75\" onerror=\"this.onerror=null;this.src='https://placehold.co/100x75/E0E0E0/333333?text=No+Feed';\" alt=\"Live Image Device %llu\"></td></tr>\n", id, ip_s, plant_name_s, pos, ts_str, cmd_s, ip_s, id);
                }
                free(t_line);
            }
            free(dev_content);
        } else { puts("<tr><td colspan=\"7\">Error: No devices found or file unreadable.</td></tr>\n"); }
        puts("</tbody></table></div>"
             "<div class=\"container\"><h2>Plants</h2><div style=\"text-align: center; margin-bottom: 15px;\">"
             "<form action=\"/cgi-bin/index.cgi\" method=\"POST\"><label for=\"plantName\">Plant Name:</label>"
             "<input type=\"text\" id=\"plantName\" name=\"plantName\" placeholder=\"e.g., Basil 1\" required>"
             "<button type=\"submit\" name=\"action\" value=\"add_plant\">Add Plant</button></form></div>"
             "<table><thead><tr><th>Name</th><th>Assign Devices</th></tr></thead><tbody>");

        char *plant_content = read_file(PLANTS_FILE);
        if (plant_content) {
            char *line, *r_line = plant_content;
            int p_idx = 0;
            while ( (line = strtok_r(r_line, "\n", &r_line)) ) {
                char *name = strtok(line, ",");
                if (name) {
                    printf("<tr><td>%s</td><td><div class=\"assign-device-cell\">"
                           "<form class=\"assign-device-row\" action=\"/cgi-bin/index.cgi\" method=\"POST\"><input type=\"hidden\" name=\"plant_index\" value=\"%d\">"
                           "<label for=\"device_id_X_%d\">X:</label><input type=\"number\" id=\"device_id_X_%d\" name=\"device_id\" placeholder=\"ID\" required min=\"0\">"
                           "<button type=\"submit\" name=\"action\" value=\"assign_device_X\">Set X</button></form>"
                           "<form class=\"assign-device-row\" action=\"/cgi-bin/index.cgi\" method=\"POST\"><input type=\"hidden\" name=\"plant_index\" value=\"%d\">"
                           "<label for=\"device_id_Y_%d\">Y:</label><input type=\"number\" id=\"device_id_Y_%d\" name=\"device_id\" placeholder=\"ID\" required min=\"0\">"
                           "<button type=\"submit\" name=\"action\" value=\"assign_device_Y\">Set Y</button></form>"
                           "<form class=\"assign-device-row\" action=\"/cgi-bin/index.cgi\" method=\"POST\"><input type=\"hidden\" name=\"plant_index\" value=\"%d\">"
                           "<label for=\"device_id_Z_%d\">Z:</label><input type=\"number\" id=\"device_id_Z_%d\" name=\"device_id\" placeholder=\"ID\" required min=\"0\">"
                           "<button type=\"submit\" name=\"action\" value=\"assign_device_Z\">Set Z</button></form>"
                           "</td></tr>\n", name, p_idx, p_idx, p_idx, p_idx, p_idx, p_idx);
                }
                p_idx++;
            }
            free(plant_content);
        } else { puts("<tr><td colspan=\"2\">Error: No plants found or file unreadable.</td></tr>\n"); }
        puts("</tbody></table></div>"
             "<div class=\"container\"><h2>Processes</h2><div class=\"button-group\">"
             "<form action=\"/cgi-bin/index.cgi\" method=\"POST\" style=\"display:inline;\">"
             "<label for=\"minutes\">Minutes:</label><input type=\"number\" id=\"minutes\" name=\"minutes\" value=\"%lld\" min=\"1\">"
             "<button type=\"submit\" name=\"action\" value=\"set_minutes\">Set Duration</button></form>"
             "<form action=\"/cgi-bin/index.cgi\" method=\"POST\" style=\"display:inline;\"><button type=\"submit\" name=\"action\" value=\"start_all_processes\">Start All</button></form>"
             "<form action=\"/cgi-bin/index.cgi\" method=\"POST\" style=\"display:inline;\"><button type=\"submit\" name=\"action\" value=\"reset_all_processes\">Reset All</button></form>"
             "</div>");
        printf("<h3 style=\"text-align: center;\">Global Process Timer: <span id=\"globalTimer\">%s</span></h3>", global_timer_status_str);
        puts("<table><thead><tr><th>Plant Name</th><th>Details</th></tr></thead><tbody>");

        char *plants_file_process_content = read_file(PLANTS_FILE);
        if (plants_file_process_content) {
            char *line_copy = strdup(plants_file_process_content);
            char *line, *r_line = line_copy;
            int p_idx = 0;
            while ( (line = strtok_r(r_line, "\n", &r_line)) ) {
                char *name, *status_s, *duration_s;
                char *t_line = strdup(line);
                name = strtok(t_line, ","); status_s = strtok(NULL, ","); duration_s = strtok(NULL, "\n");
                if (name && status_s) {
                    printf("<tr><td>%s</td><td>"
                           "<form action=\"/cgi-bin/index.cgi\" method=\"GET\" style=\"display:inline;\">"
                           "<input type=\"hidden\" name=\"plant_detail_idx\" value=\"%d\">"
                           "<button type=\"submit\">Details</button>"
                           "</form></td></tr>\n", name, p_idx);
                }
                free(t_line);
                p_idx++;
            }
            free(line_copy);
            free(plants_file_process_content);
        } else { puts("<tr><td colspan=\"2\">Error: No plants found or file unreadable.</td></tr>\n"); }
        puts("</tbody></table></div>");

        if (display_detail_plant_idx != -1) {
            MetricData current_plant_metrics = {0};
            int metrics_found = get_latest_metrics_data(display_detail_plant_idx + 1, &current_plant_metrics);

            puts("<div class=\"container\"><h2>Details</h2>");
            char *plants_content_for_details = read_file(PLANTS_FILE);
            char *detail_plant_name = "Unknown Plant";
            if (plants_content_for_details) {
                char *line_ptr = plants_content_for_details;
                int current_idx = 0;
                char line_buffer[256];
                char *temp_line_ptr;
                while(current_idx <= display_detail_plant_idx && (temp_line_ptr = strchr(line_ptr, '\n')) != NULL) {
                    size_t len = temp_line_ptr - line_ptr;
                    if (len >= sizeof(line_buffer)) len = sizeof(line_buffer) - 1;
                    strncpy(line_buffer, line_ptr, len);
                    line_buffer[len] = '\0';
                    if (current_idx == display_detail_plant_idx) {
                        char *name_token = strtok(line_buffer, ",");
                        if (name_token) { detail_plant_name = strdup(name_token); }
                        break;
                    }
                    line_ptr = temp_line_ptr + 1;
                    current_idx++;
                }
                if (current_idx == display_detail_plant_idx && detail_plant_name == (char*)"Unknown Plant" && strlen(line_ptr) > 0) {
                    strncpy(line_buffer, line_ptr, sizeof(line_buffer) - 1);
                    line_buffer[sizeof(line_buffer) - 1] = '\0';
                    char *name_token = strtok(line_buffer, ",");
                    if (name_token) { detail_plant_name = strdup(name_token); }
                }
            }
            printf("<h3>Details for %s</h3>", detail_plant_name);
            puts("<div class=\"plant-panel\"><h3>Initial Processed Images (X, Y, Z)</h3>");
            puts("<table><thead><tr><th>X Position</th><th>Y Position</th><th>Z Position</th></tr></thead><tbody><tr>");
            char img_src_x[256];
            char img_src_y[256];
            char img_src_z[256];
            snprintf(img_src_x, sizeof(img_src_x), "/data/images/plant_%d_initial_X.jpg", display_detail_plant_idx + 1);
            snprintf(img_src_y, sizeof(img_src_y), "/data/images/plant_%d_initial_Y.jpg", display_detail_plant_idx + 1);
            snprintf(img_src_z, sizeof(img_src_z), "/data/images/plant_%d_initial_Z.jpg", display_detail_plant_idx + 1);
            puts("<td>");
            printf("<img src=\"%s\" width=\"150\" height=\"150\" onerror=\"this.onerror=null;this.src='https://placehold.co/150x150/E0E0E0/333333?text=No+X+Image';\" alt=\"Initial X Image\"></td></tr>", img_src_x);
            puts("</td><td>");
            printf("<img src=\"%s\" width=\"150\" height=\"150\" onerror=\"this.onerror=null;this.src='https://placehold.co/150x150/E0E0E0/333333?text=No+Y+Image';\" alt=\"Initial Y Image\"></td></tr>", img_src_y);
            puts("</td><td>");
            printf("<img src=\"%s\" width=\"150\" height=\"150\" onerror=\"this.onerror=null;this.src='https://placehold.co/150x150/E0E0E0/333333?text=No+Z+Image';\" alt=\"Initial Z Image\"></td></tr>", img_src_z);
            puts("</td>");
            puts("</tr></tbody></table></div>");
            puts("<div class=\"plant-panel\"><h3>Canopy Area and Color Index (Top-Down View)</h3><table><thead><tr><th>Metric</th><th>Value</th><th>Trend / Image</th></tr></thead><tbody>");
            char canopy_area_str[32], color_index_str[32];
            if (metrics_found) {
                snprintf(canopy_area_str, sizeof(canopy_area_str), "%.2f cm^2", current_plant_metrics.canopy_area);
                snprintf(color_index_str, sizeof(color_index_str), "%.2f", current_plant_metrics.color_index);
            } else {
                strcpy(canopy_area_str, "N/A");
                strcpy(color_index_str, "N/A");
            }
            printf("<tr><td>Canopy Area (Ac)</td><td>%s</td><td><img src=\"/data/images/plant_%d_Canopy_Area_Ac_graph.png\" width=\"150\" height=\"50\" onerror=\"this.onerror=null;this.src='https://placehold.co/150x50/E0E0E0/333333?text=No+Graph';\" alt=\"Canopy Area Graph\"></td></tr>",
                   canopy_area_str, display_detail_plant_idx + 1);
            printf("<tr><td>Color Index (Ihue)</td><td>%s</td><td><img src=\"/data/images/plant_%d_Color_Index_Ihue_graph.png\" width=\"150\" height=\"50\" onerror=\"this.onerror=null;this.src='https://placehold.co/150x50/E0E0E0/333333?text=No+Graph';\" alt=\"Color Index Graph\"></td></tr>",
                   color_index_str, display_detail_plant_idx + 1);
            
            char top_orig_src[256], top_mask_src[256], top_grayscale_src[256], top_edges_src[256], top_green_src[256], top_green_filtered_src[256];
            snprintf(top_orig_src, sizeof(top_orig_src), "/data/images/plant_%d_initial_Y.jpg", display_detail_plant_idx + 1); // Top original is initial_Y
            snprintf(top_mask_src, sizeof(top_mask_src), "/data/images/plant_%d_top_mask.jpg", display_detail_plant_idx + 1);
            snprintf(top_grayscale_src, sizeof(top_grayscale_src), "/data/images/plant_%d_top_grayscale.jpg", display_detail_plant_idx + 1);
            snprintf(top_edges_src, sizeof(top_edges_src), "/data/images/plant_%d_top_edges.jpg", display_detail_plant_idx + 1);
            snprintf(top_green_src, sizeof(top_green_src), "/data/images/plant_%d_top_green.jpg", display_detail_plant_idx + 1);
            snprintf(top_green_filtered_src, sizeof(top_green_filtered_src), "/data/images/plant_%d_top_green_filtered.jpg", display_detail_plant_idx + 1);

            printf("<tr><td>Original Image (Top)</td><td></td><td><img src=\"%s\" width=\"150\" height=\"150\" onerror=\"this.onerror=null;this.src='https://placehold.co/150x150/E0E0E0/333333?text=No+Img';\" alt=\"Top-Down Original Image\"></td></tr>", top_orig_src);
            printf("<tr><td>Binary Mask (M_top)</td><td></td><td><img src=\"%s\" width=\"150\" height=\"150\" onerror=\"this.onerror=null;this.src='https://placehold.co/150x150/E0E0E0/333333?text=No+Mask';\" alt=\"Top-Down Binary Mask\"></td></tr>", top_mask_src);
            printf("<tr><td>Grayscale (Top)</td><td></td><td><img src=\"%s\" width=\"150\" height=\"150\" onerror=\"this.onerror=null;this.src='https://placehold.co/150x150/E0E0E0/333333?text=No+Grayscale';\" alt=\"Top-Down Grayscale Image\"></td></tr>", top_grayscale_src);
            printf("<tr><td>Edges (Top)</td><td></td><td><img src=\"%s\" width=\"150\" height=\"150\" onerror=\"this.onerror=null;this.src='https://placehold.co/150x150/E0E0E0/333333?text=No+Edges';\" alt=\"Top-Down Edges Image\"></td></tr>", top_edges_src);
            printf("<tr><td>Green Channel (Top)</td><td></td><td><img src=\"%s\" width=\"150\" height=\"150\" onerror=\"this.onerror=null;this.src='https://placehold.co/150x150/E0E0E0/333333?text=No+Green';\" alt=\"Top-Down Green Channel Image\"></td></tr>", top_green_src);
            printf("<tr><td>Green Filtered (Top)</td><td></td><td><img src=\"%s\" width=\"150\" height=\"150\" onerror=\"this.onerror=null;this.src='https://placehold.co/150x150/E0E0E0/333333?text=No+Green+Filtered';\" alt=\"Top-Down Green Filtered Image\"></td></tr>", top_green_filtered_src);
            puts("</tbody></table></div>");
            puts("<div class=\"plant-panel\"><h3>Height and Orthogonal Widths (Side Views)</h3><table><thead><tr><th>Metric</th><th>Value</th><th>Trend / Image</th></tr></thead><tbody>");
            char height_hp_str[32], width1_str[32], width2_str[32];
            if (metrics_found) {
                snprintf(height_hp_str, sizeof(height_hp_str), "%.2f cm", current_plant_metrics.height_hp);
                snprintf(width1_str, sizeof(width1_str), "%.2f cm", current_plant_metrics.width1);
                snprintf(width2_str, sizeof(width2_str), "%.2f cm", current_plant_metrics.width2);
            } else {
                strcpy(height_hp_str, "N/A");
                strcpy(width1_str, "N/A");
                strcpy(width2_str, "N/A");
            }
            printf("<tr><td>Height (Hp)</td><td>%s</td><td><img src=\"/data/images/plant_%d_Height_Hp_graph.png\" width=\"150\" height=\"50\" onerror=\"this.onerror=null;this.src='https://placehold.co/150x50/E0E0E0/333333?text=No+Graph';\" alt=\"Height Graph\"></td></tr>",
                   height_hp_str, display_detail_plant_idx + 1);
            printf("<tr><td>Width 1 (W1)</td><td>%s</td><td><img src=\"/data/images/plant_%d_Width_1_W1_graph.png\" width=\"150\" height=\"50\" onerror=\"this.onerror=null;this.src='https://placehold.co/150x50/E0E0E0/333333?text=No+Graph';\" alt=\"Width 1 Graph\"></td></tr>",
                   width1_str, display_detail_plant_idx + 1);
            printf("<tr><td>Width 2 (W2)</td><td>%s</td><td><img src=\"/data/images/plant_%d_Width_2_W2_graph.png\" width=\"150\" height=\"50\" onerror=\"this.onerror=null;this.src='https://placehold.co/150x50/E0E0E0/333333?text=No+Graph';\" alt=\"Width 2 Graph\"></td></tr>",
                   width2_str, display_detail_plant_idx + 1);

            char side1_orig_src[256], side1_mask_src[256], side1_grayscale_src[256], side1_edges_src[256], side1_green_src[256], side1_green_filtered_src[256];
            char side2_orig_src[256], side2_mask_src[256], side2_grayscale_src[256], side2_edges_src[256], side2_green_src[256], side2_green_filtered_src[256];
            snprintf(side1_orig_src, sizeof(side1_orig_src), "/data/images/plant_%d_initial_X.jpg", display_detail_plant_idx + 1); // Side1 original is initial_X
            snprintf(side1_mask_src, sizeof(side1_mask_src), "/data/images/plant_%d_side1_mask.jpg", display_detail_plant_idx + 1);
            snprintf(side1_grayscale_src, sizeof(side1_grayscale_src), "/data/images/plant_%d_side1_grayscale.jpg", display_detail_plant_idx + 1);
            snprintf(side1_edges_src, sizeof(side1_edges_src), "/data/images/plant_%d_side1_edges.jpg", display_detail_plant_idx + 1);
            snprintf(side1_green_src, sizeof(side1_green_src), "/data/images/plant_%d_side1_green.jpg", display_detail_plant_idx + 1);
            snprintf(side1_green_filtered_src, sizeof(side1_green_filtered_src), "/data/images/plant_%d_side1_green_filtered.jpg", display_detail_plant_idx + 1);

            snprintf(side2_orig_src, sizeof(side2_orig_src), "/data/images/plant_%d_initial_Z.jpg", display_detail_plant_idx + 1); // Side2 original is initial_Z
            snprintf(side2_mask_src, sizeof(side2_mask_src), "/data/images/plant_%d_side2_mask.jpg", display_detail_plant_idx + 1);
            snprintf(side2_grayscale_src, sizeof(side2_grayscale_src), "/data/images/plant_%d_side2_grayscale.jpg", display_detail_plant_idx + 1);
            snprintf(side2_edges_src, sizeof(side2_edges_src), "/data/images/plant_%d_side2_edges.jpg", display_detail_plant_idx + 1);
            snprintf(side2_green_src, sizeof(side2_green_src), "/data/images/plant_%d_side2_green.jpg", display_detail_plant_idx + 1);
            snprintf(side2_green_filtered_src, sizeof(side2_green_filtered_src), "/data/images/plant_%d_side2_green_filtered.jpg", display_detail_plant_idx + 1);

            printf("<tr><td>Original Image (Side 1)</td><td></td><td><img src=\"%s\" width=\"150\" height=\"150\" onerror=\"this.onerror=null;this.src='https://placehold.co/150x150/E0E0E0/333333?text=No+Img';\" alt=\"Side 1 Original Image\"></td></tr>", side1_orig_src);
            printf("<tr><td>Binary Mask (M_side1)</td><td></td><td><img src=\"%s\" width=\"150\" height=\"150\" onerror=\"this.onerror=null;this.src='https://placehold.co/150x150/E0E0E0/333333?text=No+Mask';\" alt=\"Side 1 Binary Mask\"></td></tr>", side1_mask_src);
            printf("<tr><td>Grayscale (Side 1)</td><td></td><td><img src=\"%s\" width=\"150\" height=\"150\" onerror=\"this.onerror=null;this.src='https://placehold.co/150x150/E0E0E0/333333?text=No+Grayscale';\" alt=\"Side 1 Grayscale Image\"></td></tr>", side1_grayscale_src);
            printf("<tr><td>Edges (Side 1)</td><td></td><td><img src=\"%s\" width=\"150\" height=\"150\" onerror=\"this.onerror=null;this.src='https://placehold.co/150x150/E0E0E0/333333?text=No+Edges';\" alt=\"Side 1 Edges Image\"></td></tr>", side1_edges_src);
            printf("<tr><td>Green Channel (Side 1)</td><td></td><td><img src=\"%s\" width=\"150\" height=\"150\" onerror=\"this.onerror=null;this.src='https://placehold.co/150x150/E0E0E0/333333?text=No+Green';\" alt=\"Side 1 Green Channel Image\"></td></tr>", side1_green_src);
            printf("<tr><td>Green Filtered (Side 1)</td><td></td><td><img src=\"%s\" width=\"150\" height=\"150\" onerror=\"this.onerror=null;this.src='https://placehold.co/150x150/E0E0E0/333333?text=No+Green+Filtered';\" alt=\"Side 1 Green Filtered Image\"></td></tr>", side1_green_filtered_src);
            printf("<tr><td>Original Image (Side 2)</td><td></td><td><img src=\"%s\" width=\"150\" height=\"150\" onerror=\"this.onerror=null;this.src='https://placehold.co/150x150/E0E0E0/333333?text=No+Img';\" alt=\"Side 2 Original Image\"></td></tr>", side2_orig_src);
            printf("<tr><td>Binary Mask (M_side2)</td><td></td><td><img src=\"%s\" width=\"150\" height=\"150\" onerror=\"this.onerror=null;this.src='https://placehold.co/150x150/E0E0E0/333333?text=No+Mask';\" alt=\"Side 2 Binary Mask\"></td></tr>", side2_mask_src);
            printf("<tr><td>Grayscale (Side 2)</td><td></td><td><img src=\"%s\" width=\"150\" height=\"150\" onerror=\"this.onerror=null;this.src='https://placehold.co/150x150/E0E0E0/333333?text=No+Grayscale';\" alt=\"Side 2 Grayscale Image\"></td></tr>", side2_grayscale_src);
            printf("<tr><td>Edges (Side 2)</td><td></td><td><img src=\"%s\" width=\"150\" height=\"150\" onerror=\"this.onerror=null;this.src='https://placehold.co/150x150/E0E0E0/333333?text=No+Edges';\" alt=\"Side 2 Edges Image\"></td></tr>", side2_edges_src);
            printf("<tr><td>Green Channel (Side 2)</td><td></td><td><img src=\"%s\" width=\"150\" height=\"150\" onerror=\"this.onerror=null;this.src='https://placehold.co/150x150/E0E0E0/333333?text=No+Green';\" alt=\"Side 2 Green Channel Image\"></td></tr>", side2_green_src);
            printf("<tr><td>Green Filtered (Side 2)</td><td></td><td><img src=\"%s\" width=\"150\" height=\"150\" onerror=\"this.onerror=null;this.src='https://placehold.co/150x150/E0E0E0/333333?text=No+Green+Filtered';\" alt=\"Side 2 Green Filtered Image\"></td></tr>", side2_green_filtered_src);
            puts("</tbody></table></div>");
            puts("<div class=\"plant-panel\"><h3>Volumetric Estimation (Voxel Sculpting)</h3><table><thead><tr><th>Metric</th><th>Value</th><th>Trend / Image</th></tr></thead><tbody>");
            char volumetric_proxy_str[32];
            if (metrics_found) {
                snprintf(volumetric_proxy_str, sizeof(volumetric_proxy_str), "%.2f cm^3", current_plant_metrics.volumetric_proxy);
            } else {
                strcpy(volumetric_proxy_str, "N/A");
            }
            printf("<tr><td>Volumetric Proxy (Vp)</td><td>%s</td><td><img src=\"/data/images/plant_%d_Volumetric_Proxy_Vp_graph.png\" width=\"150\" height=\"50\" onerror=\"this.onerror=null;this.src='https://placehold.co/150x50/E0E0E0/333333?text=No+Graph';\" alt=\"Volumetric Proxy Graph\"></td></tr>",
                   volumetric_proxy_str, display_detail_plant_idx + 1);
            
            // Color Index is already displayed above, no need to duplicate here.
            printf("<tr><td>Color Index (Ihue)</td><td>%s</td><td><img src=\"/data/images/plant_%d_Color_Index_Ihue_graph.png\" width=\"150\" height=\"50\" onerror=\"this.onerror=null;this.src='https://placehold.co/150x50/E0E0E0/333333?text=No+Graph';\" alt=\"Color Index Graph\"></td></tr>",
                   color_index_str, display_detail_plant_idx + 1); // Re-using color_index_str from above

            char volumetric_render_src[256];
            snprintf(volumetric_render_src, sizeof(volumetric_render_src), "/data/images/plant_%d_3d_render.png", display_detail_plant_idx + 1);
            printf("<tr><td>3D Reconstructed Model</td><td></td><td><img src=\"%s\" width=\"150\" height=\"150\" onerror=\"this.onerror=null;this.src='https://placehold.co/150x150/E0E0E0/333333?text=No+3D+Model';\" alt=\"3D Reconstructed Model\"></td></tr>", volumetric_render_src);
            puts("</tbody></table></div>");
            if (detail_plant_name != (char*)"Unknown Plant") { free(detail_plant_name); }
            if (plants_content_for_details) { free(plants_content_for_details); }
            puts("</div>");
        }
        puts("</body></html>");
    }
    free_plant_names_lookup();
    return 0;
}
