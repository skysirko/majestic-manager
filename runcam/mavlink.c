#include "mavlink.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

enum {
    MAVLINK_MSG_ID_HEARTBEAT = 0,
    MAVLINK_MSG_ID_STATUSTEXT = 253,
};

enum {
    MAVLINK_CRC_EXTRA_HEARTBEAT = 50,
    MAVLINK_CRC_EXTRA_STATUSTEXT = 83,
};

struct mavlink_message {
    uint32_t msgid;
    uint8_t sysid;
    uint8_t compid;
    uint8_t payload_len;
    uint8_t payload[255];
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

static int g_mavlink_fd = -1;
static uint8_t g_seq = 0;
static struct mavlink_parser g_parser;

static void mavlink_parser_reset(struct mavlink_parser *parser);
static bool mavlink_parser_feed(struct mavlink_parser *parser, uint8_t byte, struct mavlink_message *msg);
static uint16_t mavlink_crc_accumulate_buffer(const uint8_t *buf, size_t len, uint16_t crc);
static int mavlink_read_message_by_id(uint32_t target_msgid,
                                      int timeout_ms,
                                      struct mavlink_message *out_msg);
static double monotonic_seconds(void);

/* write wrapper that retries on EINTR so callers don't need to care */
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

static bool lookup_crc_extra(uint32_t msgid, uint8_t *extra) {
    switch (msgid) {
        case MAVLINK_MSG_ID_HEARTBEAT:
            *extra = MAVLINK_CRC_EXTRA_HEARTBEAT;
            return true;
        case MAVLINK_MSG_ID_STATUSTEXT:
            *extra = MAVLINK_CRC_EXTRA_STATUSTEXT;
            return true;
        default:
            return false;
    }
}

static bool configure_serial(int fd, speed_t speed) {
    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        perror("tcgetattr");
        return false;
    }
    cfmakeraw(&tty);
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);
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

bool mavlink_init(void) {
    mavlink_close();

    int fd = open(MATEK_DEVICE, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        return false;
    }
    if (!configure_serial(fd, SERIAL_SPEED)) {
        close(fd);
        return false;
    }
    g_mavlink_fd = fd;
    g_seq = 0;
    mavlink_parser_reset(&g_parser);

    printf("waiting for heartbeat from autopilot...\n");
    double last_heartbeat = 0.0;
    while (true) {
        double now = monotonic_seconds();
        if (now - last_heartbeat >= 1.0) {
            if (!mavlink_send_heartbeat()) {
                goto fail;
            }
            last_heartbeat = now;
        }
        int status = mavlink_wait_heartbeat(200);
        if (status < 0) {
            goto fail;
        }
        if (status > 0) {
            break;
        }
    }
    printf("!!! heartbeat received !!!\n");
    return true;

fail:
    mavlink_close();
    return false;
}

void mavlink_close(void) {
    if (g_mavlink_fd >= 0) {
        close(g_mavlink_fd);
        g_mavlink_fd = -1;
    }
    g_seq = 0;
    mavlink_parser_reset(&g_parser);
}

static void mavlink_parser_reset(struct mavlink_parser *parser) {
    parser->state = STATE_WAIT_STX;
    parser->header_pos = 0;
    parser->payload_pos = 0;
    parser->payload_len = 0;
    parser->signature_pos = 0;
    parser->signed_frame = false;
}

/*
 * Streaming MAVLink v2 parser: feed it one byte at a time and it will return
 * true when a full, CRC-checked message is assembled. The function implements
 * the standard MAVLink state machine (start-of-frame -> header -> payload ->
 * CRC -> optional signature), so call sites can treat the serial port as a raw
 * byte stream.
 */
static bool mavlink_parser_feed(struct mavlink_parser *parser, uint8_t byte, struct mavlink_message *msg) {
    switch (parser->state) {
        case STATE_WAIT_STX:
            if (byte == MAVLINK_V2_STX) {
                parser->header_len_expected = MAVLINK_V2_HEADER_LEN;
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
                    mavlink_parser_reset(parser);
                    break;
                }
                parser->incompat_flags = parser->header[1];
                parser->signed_frame = (parser->incompat_flags & 0x01U) != 0;
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
            if (parser->signed_frame) {
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
        msg->payload_len = parser->payload_len;
        memcpy(msg->payload, parser->payload, parser->payload_len);
        uint8_t bytes_header[10];
        memcpy(bytes_header, parser->header, MAVLINK_V2_HEADER_LEN);

        uint16_t crc = 0xFFFF;
        crc = mavlink_crc_accumulate_buffer(bytes_header, MAVLINK_V2_HEADER_LEN, crc);
        crc = mavlink_crc_accumulate_buffer(parser->payload, parser->payload_len, crc);

        msg->sysid = parser->header[4];
        msg->compid = parser->header[5];
        msg->msgid = (uint32_t)parser->header[6] |
                     ((uint32_t)parser->header[7] << 8) |
                     ((uint32_t)parser->header[8] << 16);

        uint8_t extra = 0;
        if (!lookup_crc_extra(msg->msgid, &extra)) {
            return false;
        }
        crc = mavlink_crc_accumulate_buffer(&extra, 1, crc);
        if (crc == parser->crc_received) {
            return true;
        }
        return false;
    }
}

/*
 * Straight port of the MAVLink CRC routine; matches the upstream code so our
 * checksums are accepted by autopilots. MAVLink (the drone control protocol)
 * protects each frame with a 16-bit X25 CRC (polynomial 0x1021). Instead of
 * using a lookup table, the reference algorithm applies three XOR/shift
 * operations per byte. Reproducing the same bit fiddling keeps us in sync with
 * the rest of the MAVLink ecosystem. CRC stands for Cyclic Redundancy Check,
 * the standard “parity on steroids” technique for spotting corrupted data.
 */
static uint16_t mavlink_crc_accumulate_buffer(const uint8_t *buf, size_t len, uint16_t crc) {
    for (size_t i = 0; i < len; ++i) {
        uint8_t tmp = buf[i] ^ (uint8_t)(crc & 0xFF);
        tmp ^= (tmp << 4);
        crc = (crc >> 8) ^ (tmp << 8) ^ (tmp << 3) ^ (tmp >> 4);
    }
    return crc;
}

/* Serialize and send a heartbeat frame for the provided system/component IDs. */
bool mavlink_send_heartbeat(void) {
    if (g_mavlink_fd < 0) {
        return false;
    }
    uint8_t payload[9];
    uint32_t custom_mode = 0;
    memcpy(payload, &custom_mode, sizeof(custom_mode));
    payload[4] = 18; /* MAV_TYPE_ONBOARD_CONTROLLER */
    payload[5] = 8;  /* MAV_AUTOPILOT_INVALID */
    payload[6] = 0;  /* base mode */
    payload[7] = 0;  /* system status */
    payload[8] = 3;  /* MAV_STATE_STANDBY */

    uint8_t frame[32];
    size_t offset = 0;
    uint16_t crc = 0xFFFF;

    /* MAVLink v2 header layout */
    frame[offset++] = MAVLINK_V2_STX;
    frame[offset++] = sizeof(payload);
    frame[offset++] = 0;  /* incompat */
    frame[offset++] = 0;  /* compat */
    frame[offset++] = g_seq++;
    frame[offset++] = MAVLINK_SYSTEM_ID;
    frame[offset++] = MAVLINK_COMPONENT_ID;
    frame[offset++] = 0;
    frame[offset++] = 0;
    frame[offset++] = 0;
    memcpy(&frame[offset], payload, sizeof(payload));
    offset += sizeof(payload);

    crc = mavlink_crc_accumulate_buffer(&frame[1], 9, crc);

    crc = mavlink_crc_accumulate_buffer(payload, sizeof(payload), crc);
    uint8_t extra = MAVLINK_CRC_EXTRA_HEARTBEAT; /* MAVLink heartbeat CRC extra byte */
    crc = mavlink_crc_accumulate_buffer(&extra, 1, crc);
    frame[offset++] = (uint8_t)(crc & 0xFF);
    frame[offset++] = (uint8_t)(crc >> 8);

    return write_all(g_mavlink_fd, frame, offset);
}

static int mavlink_read_message_by_id(uint32_t target_msgid,
                                      int timeout_ms,
                                      struct mavlink_message *out_msg) {
    if (g_mavlink_fd < 0) {
        return -1;
    }
    struct pollfd pfd = {.fd = g_mavlink_fd, .events = POLLIN};
    int events = poll(&pfd, 1, timeout_ms);
    if (events <= 0 || !(pfd.revents & POLLIN)) {
        return 0;
    }

    uint8_t buffer[256];
    ssize_t read_len = read(g_mavlink_fd, buffer, sizeof(buffer));
    if (read_len > 0) {
        for (ssize_t i = 0; i < read_len; ++i) {
            struct mavlink_message msg;
            if (mavlink_parser_feed(&g_parser, buffer[i], &msg) && msg.msgid == target_msgid) {
                if (out_msg) {
                    *out_msg = msg;
                }
                return 1;
            }
        }
        return 0;
    }
    if (read_len == 0 || errno == EAGAIN || errno == EINTR) {
        return 0;
    }
    perror("read");
    return -1;
}

int mavlink_wait_heartbeat(int timeout_ms) {
    int status = mavlink_read_message_by_id(MAVLINK_MSG_ID_HEARTBEAT, timeout_ms, NULL);
    if (status > 0) {
        mavlink_parser_reset(&g_parser);
    }
    return status;
}

int mavlink_read_statustext(int timeout_ms, struct mavlink_statustext *out_msg) {
    struct mavlink_message msg;
    int status = mavlink_read_message_by_id(MAVLINK_MSG_ID_STATUSTEXT, timeout_ms, &msg);
    if (status <= 0) {
        return status;
    }
    if (msg.payload_len < 1) {
        return 0;
    }

    if (out_msg) {
        size_t text_len = 0;
        out_msg->severity = msg.payload[0];
        out_msg->text_id = 0;
        out_msg->chunk_seq = 0;
        if (msg.payload_len >= 54) {
            text_len = MAVLINK_STATUSTEXT_MAX_LEN;
            out_msg->text_id = (uint16_t)msg.payload[51] | ((uint16_t)msg.payload[52] << 8);
            out_msg->chunk_seq = msg.payload[53];
        } else {
            if (msg.payload_len > 1) {
                text_len = msg.payload_len - 1;
            }
            if (text_len > MAVLINK_STATUSTEXT_MAX_LEN) {
                text_len = MAVLINK_STATUSTEXT_MAX_LEN;
            }
        }
        memcpy(out_msg->text, &msg.payload[1], text_len);
        out_msg->text[text_len] = '\0';
        char *newline = strchr(out_msg->text, '\n');
        if (newline) {
            *newline = '\0';
        }
    }
    return 1;
}

static double monotonic_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}
