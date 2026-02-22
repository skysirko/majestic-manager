#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

// MAVLink's generated enums use values beyond ISO C's int range, which triggers
// -Wpedantic on both clang and GCC. Temporarily silence that warning while the
// header is included, then restore prior settings immediately afterward.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpedantic"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

#include "third_party/c_library_v2/common/mavlink.h"

// Restore the prior warning configuration immediately after the include so the
// rest of this translation unit is still built with full -Wpedantic checking.
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "matek_mavlink.h"

static const char *const MATEK_DEVICE = "/dev/ttyS2";
static const speed_t SERIAL_SPEED = B57600;
static const uint8_t SYSTEM_ID = 2;
static const uint8_t COMPONENT_ID = 191;
static mavlink_status_t parser_status;

static int configure_serial(int fd) {
    struct termios tty;

    if (tcgetattr(fd, &tty) != 0) {
        fprintf(stderr, "tcgetattr failed: %s\n", strerror(errno));
        return -1;
    }

    cfmakeraw(&tty);
    cfsetispeed(&tty, SERIAL_SPEED);
    cfsetospeed(&tty, SERIAL_SPEED);

    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~(PARENB | PARODD | CSTOPB);
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        fprintf(stderr, "tcsetattr failed: %s\n", strerror(errno));
        return -1;
    }

    if (tcflush(fd, TCIFLUSH) != 0) {
        fprintf(stderr, "tcflush failed: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

static int write_all(int fd, const uint8_t *data, size_t length) {
    size_t offset = 0;

    while (offset < length) {
        const ssize_t written = write(fd, data + offset, length - offset);

        if (written < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                // Retry on signal interruption or when serial port reports "try again".
                continue;
            }

            return -1;
        }

        offset += (size_t)written;
    }

    return 0;
}

int open_matek_device(void) {
    const int fd = open(MATEK_DEVICE, O_RDWR | O_NOCTTY | O_SYNC);

    if (fd < 0) {
        fprintf(stderr, "Unable to open %s: %s\n", MATEK_DEVICE, strerror(errno));
        return -1;
    }

    if (configure_serial(fd) != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

int send_heartbeat(int fd) {
    mavlink_message_t message;
    uint8_t buffer[MAVLINK_MAX_PACKET_LEN];

    mavlink_msg_heartbeat_pack(
        SYSTEM_ID,
        COMPONENT_ID,
        &message,
        MAV_TYPE_ONBOARD_CONTROLLER,
        MAV_AUTOPILOT_INVALID,
        0,
        0,
        MAV_STATE_ACTIVE);

    const uint16_t length = mavlink_msg_to_send_buffer(buffer, &message);

    if (write_all(fd, buffer, length) != 0) {
        fprintf(stderr, "Failed to write heartbeat: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

int receive_statustext(int fd, matek_statustext_t *message) {
    if (message == NULL) {
        errno = EINVAL;
        return -1;
    }

    uint8_t buffer[128];
    const ssize_t bytes_read = read(fd, buffer, sizeof(buffer));

    if (bytes_read < 0) {
        // Non-fatal: interrupted read or no bytes available yet on non-blocking fd.
        if (errno == EINTR || errno == EAGAIN) {
            return 0;
        }

        fprintf(stderr, "Failed to read from Matek link: %s\n", strerror(errno));
        return -1;
    }

    if (bytes_read == 0) {
        return 0;
    }

    mavlink_message_t parsed;
    int found = 0;

    for (ssize_t i = 0; i < bytes_read; ++i) {
        if (mavlink_parse_char(MAVLINK_COMM_0, buffer[i], &parsed, &parser_status)) {
            if (parsed.msgid == MAVLINK_MSG_ID_STATUSTEXT) {
                mavlink_statustext_t decoded;
                mavlink_msg_statustext_decode(&parsed, &decoded);

                message->severity = decoded.severity;
                message->id = decoded.id;
                message->chunk_seq = decoded.chunk_seq;

                memcpy(message->text, decoded.text, MATEK_STATUSTEXT_MAX_LEN);
                message->text[MATEK_STATUSTEXT_MAX_LEN] = '\0';

                found = 1;
                break;
            }
        }
    }

    return found;
}
