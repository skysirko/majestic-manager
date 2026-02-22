#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "matek_mavlink.h"
#include "majestic_config.h"

static const char *const CROPS[] = {
    "0x0x1920x1080",
    "480x270x960x540",
    "720x405x480x270",
    "840x472x240x135",
};

static const char *const DEFAULT_MAJESTIC_CONFIG = "/etc/majestic.yaml";
static size_t current_crop_index = 0;
static const size_t CROP_COUNT = sizeof(CROPS) / sizeof(CROPS[0]);

static int apply_crop_index(size_t new_index) {
    if (new_index >= CROP_COUNT) {
        return -1;
    }

    if (majestic_config_set_crop(DEFAULT_MAJESTIC_CONFIG, CROPS[new_index]) != 0) {
        fprintf(stderr, "Failed to update Majestic crop to %s\n", CROPS[new_index]);
        return -1;
    }

    current_crop_index = new_index;
    return 0;
}

static void handle_zoom_command(const char *command) {
    if (strcmp(command, "zoom_in") == 0) {
        if (current_crop_index + 1 < CROP_COUNT) {
            (void)apply_crop_index(current_crop_index + 1);
        }
        return;
    }

    if (strcmp(command, "zoom_out") == 0) {
        if (current_crop_index > 0) {
            (void)apply_crop_index(current_crop_index - 1);
        }
    }
}

static void handle_statustext(const matek_statustext_t *message) {
    fprintf(stderr, "STATUSTEXT (severity=%u id=%u chunk=%u): %s\n",
        message->severity,
        message->id,
        message->chunk_seq,
        message->text
    );

    handle_zoom_command(message->text);
}

static uint64_t monotonic_now_ms(void) {
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }

    return (uint64_t)now.tv_sec * 1000ULL + (uint64_t)now.tv_nsec / 1000000ULL;
}

static int event_loop(int fd) {
    const uint64_t interval_ms = 1000;
    uint64_t next_emit_ms = 0;

    while (1) {
        const uint64_t now_ms = monotonic_now_ms();

        if (next_emit_ms == 0 || now_ms >= next_emit_ms) {
            if (send_heartbeat(fd) != 0) {
                return -1;
            }

            next_emit_ms = now_ms + interval_ms;
        }

        matek_statustext_t status_message;
        const int statustext_result = receive_statustext(fd, &status_message);

        if (statustext_result < 0) {
            return -1;
        }

        if (statustext_result > 0) {
            handle_statustext(&status_message);
        }

        const struct timespec sleep_time = {
            .tv_sec = 0,
            .tv_nsec = 10 * 1000 * 1000
        };

        nanosleep(&sleep_time, NULL);
    }

    return 0;
}

int main(void) {
    if (apply_crop_index(0) != 0) {
        fprintf(stderr, "Unable to prime Majestic configuration.\n");
    }

    const int matek_fd = open_matek_device();

    if (matek_fd < 0) {
        return EXIT_FAILURE;
    }

    const int loop_status = event_loop(matek_fd);

    close(matek_fd);

    return loop_status == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
