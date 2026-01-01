#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "mavlink.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static const char *const CROPS[] = {
    "0x0x3840x2160",
    "640x360x3200x1800",
    "1280x720x2560x1440",
    "1600x820x2240x1340",
};

static const char *const MATEK_DEVICE = "/dev/ttyS2";
static const char *const DEFAULT_MAJESTIC_CONFIG = "/etc/majestic.yaml";
static const speed_t SERIAL_SPEED = B57600;
static const uint8_t SYSTEM_ID = 2;
static const uint8_t COMPONENT_ID = 191; /* MAV_COMP_ID_ONBOARD_COMPUTER */

struct line_buffer {
    char **lines;
    size_t count;
    size_t capacity;
};

static const char *config_path;
static size_t crop_index = 0;

static double monotonic_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void line_buffer_free(struct line_buffer *buffer) {
    for (size_t i = 0; i < buffer->count; ++i) {
        free(buffer->lines[i]);
    }
    free(buffer->lines);
    buffer->lines = NULL;
    buffer->count = buffer->capacity = 0;
}

static bool line_buffer_push(struct line_buffer *buffer, const char *line, size_t len) {
    if (buffer->count == buffer->capacity) {
        size_t new_capacity = buffer->capacity ? buffer->capacity * 2 : 32;
        char **new_lines = realloc(buffer->lines, new_capacity * sizeof(char *));
        if (!new_lines) {
            return false;
        }
        buffer->lines = new_lines;
        buffer->capacity = new_capacity;
    }
    buffer->lines[buffer->count] = strndup(line, len);
    if (!buffer->lines[buffer->count]) {
        return false;
    }
    buffer->count += 1;
    return true;
}

static int run_command(char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        return -1;
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        return 0;
    }
    return -1;
}

static void reload_majestic(void) {
    char *cmd1[] = {"killall", "-1", "majestic", NULL};
    char *cmd2[] = {"killall", "majestic", NULL};
    if (run_command(cmd1) == 0) {
        return;
    }
    if (run_command(cmd2) == 0) {
        return;
    }
    fprintf(stderr, "Unable to signal Majestic; crop change may require manual restart.\n");
}

static bool set_crop_in_config(const char *crop, bool ensure_exists) {
    FILE *file = fopen(config_path, "r");
    if (!file) {
        fprintf(stderr, "Majestic config not found at %s; skipping crop update.\n", config_path);
        return false;
    }

    struct line_buffer buffer = {0};
    char *line = NULL;
    size_t line_cap = 0;
    ssize_t line_len;
    while ((line_len = getline(&line, &line_cap, file)) != -1) {
        if (!line_buffer_push(&buffer, line, (size_t)line_len)) {
            fprintf(stderr, "Out of memory while reading config.\n");
            free(line);
            fclose(file);
            line_buffer_free(&buffer);
            return false;
        }
    }
    free(line);
    fclose(file);

    bool in_video0 = false;
    size_t section_indent = 0;
    ssize_t insert_index = -1;
    bool updated = false;

    for (size_t i = 0; i < buffer.count; ++i) {
        char *current = buffer.lines[i];
        size_t len = strlen(current);
        size_t indent = 0;
        while (indent < len && (current[indent] == ' ' || current[indent] == '\t')) {
            indent++;
        }
        const char *trimmed = current + indent;

        if (strncmp(trimmed, "video0:", 7) == 0) {
            in_video0 = true;
            section_indent = indent;
            insert_index = (ssize_t)i + 1;
            continue;
        }

        if (in_video0 && indent <= section_indent && trimmed[0] != '\0') {
            break;
        }

        if (in_video0 && strncmp(trimmed, "crop:", 5) == 0) {
            char *newline = strchr(current, '\n');
            size_t new_len = indent + strlen("crop: ") + strlen(crop) + 2;
            char *replacement = malloc(new_len);
            if (!replacement) {
                line_buffer_free(&buffer);
                return false;
            }
            if (newline) {
                *newline = '\0';
            }
            snprintf(replacement, new_len, "%.*s%s%s\n", (int)indent, current, "crop: ", crop);
            free(buffer.lines[i]);
            buffer.lines[i] = replacement;
            updated = true;
            break;
        }
    }

    if (!updated) {
        if (ensure_exists && insert_index >= 0) {
            size_t indent_len = section_indent + 2;
            char *insert_line = malloc(indent_len + strlen("crop: ") + strlen(crop) + 2);
            if (!insert_line) {
                line_buffer_free(&buffer);
                return false;
            }
            memset(insert_line, ' ', indent_len);
            insert_line[indent_len] = '\0';
            snprintf(insert_line + indent_len, strlen("crop: ") + strlen(crop) + 2, "crop: %s\n", crop);

            if (buffer.count == buffer.capacity) {
                size_t new_capacity = buffer.capacity ? buffer.capacity * 2 : 32;
                char **new_lines = realloc(buffer.lines, new_capacity * sizeof(char *));
                if (!new_lines) {
                    free(insert_line);
                    line_buffer_free(&buffer);
                    return false;
                }
                buffer.lines = new_lines;
                buffer.capacity = new_capacity;
            }
            size_t move_count = buffer.count - (size_t)insert_index;
            memmove(&buffer.lines[insert_index + 1], &buffer.lines[insert_index],
                    move_count * sizeof(char *));
            buffer.lines[insert_index] = insert_line;
            buffer.count += 1;
            updated = true;
        } else {
            fprintf(stderr, "crop entry inside video0 not found; no changes written.\n");
        }
    }

    if (updated) {
        FILE *out = fopen(config_path, "w");
        if (!out) {
            perror("fopen");
            line_buffer_free(&buffer);
            return false;
        }
        for (size_t i = 0; i < buffer.count; ++i) {
            fputs(buffer.lines[i], out);
        }
        fclose(out);
        reload_majestic();
    }

    line_buffer_free(&buffer);
    return updated;
}

static bool configure_serial(int fd) {
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        perror("tcgetattr");
        return false;
    }
    cfmakeraw(&tty);
    cfsetispeed(&tty, SERIAL_SPEED);
    cfsetospeed(&tty, SERIAL_SPEED);
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;
    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr");
        return false;
    }
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0) {
        perror("fcntl");
        return false;
    }
    flags |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) < 0) {
        perror("fcntl");
        return false;
    }
    return true;
}

static int connect_to_matek(void) {
    int fd = open(MATEK_DEVICE, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        return -1;
    }
    if (!configure_serial(fd)) {
        close(fd);
        return -1;
    }
    return fd;
}

static void apply_crop_index(void) {
    if (crop_index >= ARRAY_SIZE(CROPS)) {
        crop_index = ARRAY_SIZE(CROPS) - 1;
    }
    const char *crop = CROPS[crop_index];
    set_crop_in_config(crop, crop_index == 0);
}

static void execute_command(const char *command) {
    if (strcmp(command, "zoom_in") == 0) {
        if (crop_index + 1 < ARRAY_SIZE(CROPS)) {
            crop_index++;
            set_crop_in_config(CROPS[crop_index], false);
        }
        return;
    }
    if (strcmp(command, "zoom_out") == 0) {
        if (crop_index > 0) {
            crop_index--;
            set_crop_in_config(CROPS[crop_index], false);
        }
        return;
    }
    if (strcmp(command, "day_mode") == 0) {
        char *bitrate[] = {"curl", "-s", "http://localhost/api/v1/set?video1.bitrate={900}", NULL};
        char *night_on[] = {"curl", "-s", "http://localhost/night/on", NULL};
        int set_status = run_command(bitrate);
        int night_status = run_command(night_on);
        if (set_status == 0 && night_status == 0) {
            printf("day_mode curl commands succeeded\n");
        }
        return;
    }
    if (strcmp(command, "night_mode") == 0) {
        char *bitrate[] = {"curl", "-s", "http://localhost/api/v1/set?video1.bitrate={570}", NULL};
        char *night_off[] = {"curl", "-s", "http://localhost/night/off", NULL};
        int set_status = run_command(bitrate);
        int night_status = run_command(night_off);
        if (set_status == 0 && night_status == 0) {
            printf("night_mode curl commands succeeded\n");
        }
        return;
    }
}

static void handle_message(const struct mavlink_message *msg) {
    if (msg->msgid != MAVLINK_MSG_ID_STATUSTEXT || msg->payload_len < 2) {
        return;
    }
    uint8_t severity = msg->payload[0];
    uint16_t text_id = 0;
    uint8_t chunk_seq = 0;
    size_t text_len = 0;
    if (msg->payload_len >= 54) {
        text_len = 50;
        text_id = (uint16_t)msg->payload[51] | ((uint16_t)msg->payload[52] << 8);
        chunk_seq = msg->payload[53];
    } else {
        if (msg->payload_len > 1) {
            text_len = msg->payload_len - 1;
        }
        if (text_len > 50) {
            text_len = 50;
        }
    }

    char text[51];
    memcpy(text, &msg->payload[1], text_len);
    text[text_len] = '\0';
    char *newline = strchr(text, '\n');
    if (newline) {
        *newline = '\0';
    }

    if (chunk_seq != 0) {
        return;
    }

    if (strcmp(text, "zoom_in") == 0 || strcmp(text, "zoom_out") == 0 ||
        strcmp(text, "day_mode") == 0 || strcmp(text, "night_mode") == 0) {
        printf("[STATUSTEXT severity=%u id=%u chunk=%u] %s\n", severity, text_id, chunk_seq, text);
        execute_command(text);
    }
}

static void event_loop(int fd) {
    struct mavlink_parser parser = {0};
    mavlink_parser_reset(&parser);
    uint8_t seq = 0;
    double last_heartbeat = 0.0;
    bool heartbeat_received = false;

    printf("waiting for heartbeat from autopilot...\n");
    while (!heartbeat_received) {
        double now = monotonic_seconds();
        if (now - last_heartbeat >= 1.0) {
            mavlink_send_heartbeat(fd, seq++, SYSTEM_ID, COMPONENT_ID);
            last_heartbeat = now;
        }
        int status = mavlink_read_message_by_id(fd, &parser, MAVLINK_MSG_ID_HEARTBEAT, 200, NULL);
        if (status < 0) {
            return;
        }
        if (status > 0) {
            heartbeat_received = true;
        }
    }
    printf("!!! heartbeat received !!!\n");

    apply_crop_index();

    mavlink_parser_reset(&parser);
    last_heartbeat = 0.0;
    while (true) {
        double now = monotonic_seconds();
        if (now - last_heartbeat >= 1.0) {
            mavlink_send_heartbeat(fd, seq++, SYSTEM_ID, COMPONENT_ID);
            last_heartbeat = now;
        }
        struct mavlink_message msg;
        int status = mavlink_read_message_by_id(fd, &parser, MAVLINK_MSG_ID_STATUSTEXT, 100, &msg);
        if (status < 0) {
            break;
        }
        if (status > 0) {
            handle_message(&msg);
        }
    }
}

int main(void) {
    const char *env_path = getenv("MAJESTIC_CONFIG_PATH");
    config_path = env_path ? env_path : DEFAULT_MAJESTIC_CONFIG;

    int fd = connect_to_matek();
    if (fd < 0) {
        return 1;
    }
    event_loop(fd);
    close(fd);
    return 0;
}
