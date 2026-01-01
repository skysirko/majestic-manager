#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
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

static const uint8_t MAVLINK_V2_STX = 0xFD;
static const uint8_t MAVLINK_V1_STX = 0xFE;
static const uint8_t MAVLINK_SIGNATURE_LEN = 13;

enum {
    MAVLINK_MSG_ID_HEARTBEAT = 0,
    MAVLINK_MSG_ID_STATUSTEXT = 253,
};

struct line_buffer {
    char **lines;
    size_t count;
    size_t capacity;
};

struct mavlink_parser {
    enum {
        STATE_WAIT_STX,
        STATE_HEADER,
        STATE_PAYLOAD,
        STATE_CRC1,
        STATE_CRC2,
        STATE_SIGNATURE
    } state;
    bool mavlink2;
    uint8_t header[10];
    size_t header_len_expected;
    size_t header_pos;
    uint8_t payload[255];
    uint8_t payload_len;
    uint8_t payload_pos;
    uint8_t incompat_flags;
    bool signed_frame;
    uint8_t signature_pos;
    uint16_t crc_received;
};

struct mavlink_message {
    uint32_t msgid;
    uint8_t sysid;
    uint8_t compid;
    uint8_t payload_len;
    bool mavlink2;
    uint8_t payload[255];
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

static uint16_t crc_accumulate_buffer(const uint8_t *buf, size_t len, uint16_t crc) {
    for (size_t i = 0; i < len; ++i) {
        uint8_t tmp = buf[i] ^ (uint8_t)(crc & 0xFF);
        tmp ^= (tmp << 4);
        crc = (crc >> 8) ^ (tmp << 8) ^ (tmp << 3) ^ (tmp >> 4);
    }
    return crc;
}

static bool lookup_crc_extra(uint32_t msgid, uint8_t *extra) {
    switch (msgid) {
        case MAVLINK_MSG_ID_HEARTBEAT:
            *extra = 50;
            return true;
        case MAVLINK_MSG_ID_STATUSTEXT:
            *extra = 83;
            return true;
        default:
            return false;
    }
}

static void parser_reset(struct mavlink_parser *parser) {
    parser->state = STATE_WAIT_STX;
    parser->header_pos = 0;
    parser->payload_pos = 0;
    parser->payload_len = 0;
    parser->signature_pos = 0;
    parser->signed_frame = false;
    parser->mavlink2 = false;
}

static bool parser_feed(struct mavlink_parser *parser, uint8_t byte, struct mavlink_message *msg) {
    switch (parser->state) {
        case STATE_WAIT_STX:
            if (byte == MAVLINK_V2_STX) {
                parser->mavlink2 = true;
                parser->header_len_expected = 9;
                parser->header_pos = 0;
                parser->state = STATE_HEADER;
            } else if (byte == MAVLINK_V1_STX) {
                parser->mavlink2 = false;
                parser->header_len_expected = 5;
                parser->header_pos = 0;
                parser->state = STATE_HEADER;
            }
            break;
        case STATE_HEADER:
            parser->header[parser->header_pos++] = byte;
            if (parser->header_pos == parser->header_len_expected) {
                parser->payload_len = parser->header[0];
                parser->payload_pos = 0;
                if (parser->payload_len > sizeof(parser->payload)) {
                    parser_reset(parser);
                    break;
                }
                if (parser->mavlink2) {
                    parser->incompat_flags = parser->header[1];
                    parser->signed_frame = (parser->incompat_flags & 0x01U) != 0;
                } else {
                    parser->signed_frame = false;
                }
                parser->state = parser->payload_len ? STATE_PAYLOAD : STATE_CRC1;
            }
            break;
        case STATE_PAYLOAD:
            parser->payload[parser->payload_pos++] = byte;
            if (parser->payload_pos == parser->payload_len) {
                parser->state = STATE_CRC1;
            }
            break;
        case STATE_CRC1:
            parser->crc_received = byte;
            parser->state = STATE_CRC2;
            break;
        case STATE_CRC2:
            parser->crc_received |= (uint16_t)byte << 8;
            if (parser->mavlink2 && parser->signed_frame) {
                parser->signature_pos = 0;
                parser->state = STATE_SIGNATURE;
            } else {
                parser->state = STATE_WAIT_STX;
                goto finalize;
            }
            break;
        case STATE_SIGNATURE:
            parser->signature_pos++;
            if (parser->signature_pos == MAVLINK_SIGNATURE_LEN) {
                parser->state = STATE_WAIT_STX;
                goto finalize;
            }
            break;
    }
    return false;

finalize: {
        msg->mavlink2 = parser->mavlink2;
        msg->payload_len = parser->payload_len;
        memcpy(msg->payload, parser->payload, parser->payload_len);
        uint8_t bytes_header[10];
        size_t count = 0;
        if (parser->mavlink2) {
            count = 9;
        } else {
            count = 5;
        }
        memcpy(bytes_header, parser->header, count);

        uint16_t crc = 0xFFFF;
        crc = crc_accumulate_buffer(bytes_header, count, crc);
        crc = crc_accumulate_buffer(parser->payload, parser->payload_len, crc);

        if (parser->mavlink2) {
            msg->sysid = parser->header[4];
            msg->compid = parser->header[5];
            msg->msgid = (uint32_t)parser->header[6] |
                         ((uint32_t)parser->header[7] << 8) |
                         ((uint32_t)parser->header[8] << 16);
        } else {
            msg->sysid = parser->header[2];
            msg->compid = parser->header[3];
            msg->msgid = parser->header[4];
        }

        uint8_t extra = 0;
        if (!lookup_crc_extra(msg->msgid, &extra)) {
            return false;
        }
        crc = crc_accumulate_buffer(&extra, 1, crc);
        if (crc == parser->crc_received) {
            return true;
        }
        return false;
    }
}

static bool write_all(int fd, const void *data, size_t len) {
    const uint8_t *ptr = data;
    while (len > 0) {
        ssize_t written = write(fd, ptr, len);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("write");
            return false;
        }
        ptr += (size_t)written;
        len -= (size_t)written;
    }
    return true;
}

static bool send_heartbeat(int fd, uint8_t seq, bool use_v2) {
    uint8_t payload[9];
    uint32_t custom_mode = 0;
    memcpy(payload, &custom_mode, sizeof(custom_mode));
    payload[4] = 18; /* MAV_TYPE_ONBOARD_CONTROLLER */
    payload[5] = 8;  /* MAV_AUTOPILOT_INVALID */
    payload[6] = 0;
    payload[7] = 0;
    payload[8] = 3;

    uint8_t frame[32];
    size_t offset = 0;
    uint16_t crc = 0xFFFF;

    if (use_v2) {
        frame[offset++] = MAVLINK_V2_STX;
        frame[offset++] = sizeof(payload);
        frame[offset++] = 0;  /* incompat */
        frame[offset++] = 0;  /* compat */
        frame[offset++] = seq;
        frame[offset++] = SYSTEM_ID;
        frame[offset++] = COMPONENT_ID;
        frame[offset++] = 0;
        frame[offset++] = 0;
        frame[offset++] = 0;
        memcpy(&frame[offset], payload, sizeof(payload));
        offset += sizeof(payload);

        crc = crc_accumulate_buffer(&frame[1], 9, crc);
    } else {
        frame[offset++] = MAVLINK_V1_STX;
        frame[offset++] = sizeof(payload);
        frame[offset++] = seq;
        frame[offset++] = SYSTEM_ID;
        frame[offset++] = COMPONENT_ID;
        frame[offset++] = MAVLINK_MSG_ID_HEARTBEAT;
        memcpy(&frame[offset], payload, sizeof(payload));
        offset += sizeof(payload);

        crc = crc_accumulate_buffer(&frame[1], 5, crc);
    }

    crc = crc_accumulate_buffer(payload, sizeof(payload), crc);
    uint8_t extra = 50;
    crc = crc_accumulate_buffer(&extra, 1, crc);
    frame[offset++] = (uint8_t)(crc & 0xFF);
    frame[offset++] = (uint8_t)(crc >> 8);

    return write_all(fd, frame, offset);
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
}

static void handle_message(const struct mavlink_message *msg) {
    if (msg->msgid != MAVLINK_MSG_ID_STATUSTEXT || msg->payload_len < 2) {
        return;
    }
    uint8_t severity = msg->payload[0];
    uint16_t text_id = 0;
    uint8_t chunk_seq = 0;
    size_t text_len = 0;
    if (msg->mavlink2 && msg->payload_len >= 54) {
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

    if (strcmp(text, "zoom_in") == 0 || strcmp(text, "zoom_out") == 0) {
        printf("[STATUSTEXT severity=%u id=%u chunk=%u] %s\n", severity, text_id, chunk_seq, text);
        execute_command(text);
    }
}

static void event_loop(int fd) {
    struct mavlink_parser parser = {0};
    parser_reset(&parser);
    uint8_t seq = 0;
    double last_heartbeat = 0.0;
    bool heartbeat_received = false;
    bool prefer_mavlink2 = false;

    printf("waiting for heartbeat from autopilot...\n");
    while (!heartbeat_received) {
        double now = monotonic_seconds();
        if (now - last_heartbeat >= 1.0) {
            send_heartbeat(fd, seq++, prefer_mavlink2);
            last_heartbeat = now;
        }
        struct pollfd pfd = {
            .fd = fd,
            .events = POLLIN,
        };
        int events = poll(&pfd, 1, 200);
        if (events > 0 && (pfd.revents & POLLIN)) {
            uint8_t buffer[128];
            ssize_t read_len = read(fd, buffer, sizeof(buffer));
            if (read_len > 0) {
                for (ssize_t i = 0; i < read_len; ++i) {
                    struct mavlink_message msg;
                    if (parser_feed(&parser, buffer[i], &msg)) {
                        if (msg.mavlink2) {
                            prefer_mavlink2 = true;
                        }
                        if (msg.msgid == MAVLINK_MSG_ID_HEARTBEAT) {
                            heartbeat_received = true;
                            break;
                        }
                    }
                }
            }
        }
    }
    printf("!!! heartbeat received !!!\n");

    apply_crop_index();

    parser_reset(&parser);
    last_heartbeat = 0.0;
    while (true) {
        double now = monotonic_seconds();
        if (now - last_heartbeat >= 1.0) {
            send_heartbeat(fd, seq++, prefer_mavlink2);
            last_heartbeat = now;
        }
        struct pollfd pfd = {.fd = fd, .events = POLLIN};
        int events = poll(&pfd, 1, 100);
        if (events > 0 && (pfd.revents & POLLIN)) {
            uint8_t buffer[256];
            ssize_t read_len = read(fd, buffer, sizeof(buffer));
            if (read_len > 0) {
                for (ssize_t i = 0; i < read_len; ++i) {
                    struct mavlink_message msg;
                    if (parser_feed(&parser, buffer[i], &msg)) {
                        if (msg.mavlink2) {
                            prefer_mavlink2 = true;
                        }
                        handle_message(&msg);
                    }
                }
            } else if (read_len == 0) {
                continue;
            } else if (errno != EAGAIN && errno != EINTR) {
                perror("read");
                break;
            }
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
