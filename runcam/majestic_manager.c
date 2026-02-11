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

#include "majestic_config.h"
#include "mavlink.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static const char *const CROPS[] = {
    "0x0x1920x1080",
    "480x270x960x540",
    "720x405x480x270",
    "840x472x240x135",
};

static const char *const MATEK_DEVICE = "/dev/ttyS2";
static const char *const DEFAULT_MAJESTIC_CONFIG = "/etc/majestic.yaml";
static const speed_t SERIAL_SPEED = B57600;
static const uint8_t SYSTEM_ID = 2;
static const uint8_t COMPONENT_ID = 191; /* MAV_COMP_ID_ONBOARD_COMPUTER */

static size_t crop_index = 0;

static double monotonic_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* majestic_config module manages config storage */

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
    if (!majestic_config_set_crop(crop, ensure_exists)) {
        return false;
    }
    reload_majestic();
    return true;
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
    if (strcmp(command, "night_mode") == 0) {
        char *bitrate[] = {"curl", "-s", "http://localhost/api/v1/set?video1.bitrate={900}", NULL};
        char *night_on[] = {"curl", "-s", "http://localhost/night/on", NULL};
        int set_status = run_command(bitrate);
        int night_status = run_command(night_on);
        if (set_status == 0 && night_status == 0) {
            printf("night_mode curl commands succeeded\n");
        }
        return;
    }
    if (strcmp(command, "day_mode") == 0) {
        char *bitrate[] = {"curl", "-s", "http://localhost/api/v1/set?video1.bitrate={570}", NULL};
        char *night_off[] = {"curl", "-s", "http://localhost/night/off", NULL};
        int set_status = run_command(bitrate);
        int night_status = run_command(night_off);
        if (set_status == 0 && night_status == 0) {
            printf("day_mode curl commands succeeded\n");
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
    const char *config_path = env_path ? env_path : DEFAULT_MAJESTIC_CONFIG;

    if (!majestic_config_init(config_path)) {
        return 1;
    }

    int fd = connect_to_matek();
    if (fd < 0) {
        majestic_config_free();
        return 1;
    }
    event_loop(fd);
    close(fd);
    majestic_config_free();
    return 0;
}
