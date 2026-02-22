#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "matek_mavlink.h"
#include "majestic_config.h"
#include "majestic_process.h"

static const char *const CROPS[] = {
    "0x0x1920x1080",
    "480x270x960x540",
    "720x405x480x270",
    "840x472x240x135",
};

static const char *const DEFAULT_MAJESTIC_CONFIG = "/etc/majestic.yaml";
static size_t current_crop_index = 0;
static const size_t CROP_INDEX_MIN = 0;
static const size_t CROP_INDEX_MAX = sizeof(CROPS) / sizeof(CROPS[0]) - 1;
static const struct timespec RECONNECT_DELAY = {
    .tv_sec = 1,
    .tv_nsec = 0
};

static int apply_crop_index(size_t new_index) {
    if (new_index > CROP_INDEX_MAX) {
        return -1;
    }

    if (majestic_config_set_crop(DEFAULT_MAJESTIC_CONFIG, CROPS[new_index]) != 0) {
        fprintf(stderr, "Failed to update Majestic crop to %s\n", CROPS[new_index]);
        return -1;
    }

    if (reload_majestic_process() != 0) {
        fprintf(stderr, "Failed to reload Majestic after updating crop.\n");
        return -1;
    }

    current_crop_index = new_index;
    return 0;
}

static void handle_statustext(const char *text) {
    if (strcmp(text, "zoom_in") == 0) {
        if (current_crop_index < CROP_INDEX_MAX) {
            (void)apply_crop_index(current_crop_index + 1);
        }
        return;
    }

    if (strcmp(text, "zoom_out") == 0) {
        if (current_crop_index > CROP_INDEX_MIN) {
            (void)apply_crop_index(current_crop_index - 1);
        }
    }
}

static uint64_t monotonic_now_ms(void) {
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }

    return (uint64_t)now.tv_sec * 1000ULL + (uint64_t)now.tv_nsec / 1000000ULL;
}

static void event_loop(int fd) {
    const uint64_t interval_ms = 1000;
    uint64_t next_emit_ms = 0;

    while (1) {
        const uint64_t now_ms = monotonic_now_ms();

        if (next_emit_ms == 0 || now_ms >= next_emit_ms) {
            if (send_heartbeat(fd) != 0) {
                return;
            }
            next_emit_ms = now_ms + interval_ms;
        }

        matek_statustext_t msg;
        const int statustext_result = receive_statustext(fd, &msg);
        if (statustext_result < 0) {
            return;
        }

        if (statustext_result > 0) {
            fprintf(stderr, "STATUSTEXT (severity=%u id=%u chunk=%u): %s\n", msg.severity, msg.id, msg.chunk_seq, msg.text);
            handle_statustext(msg.text);
        }

        const struct timespec sleep_time = {
            .tv_sec = 0,
            .tv_nsec = 10 * 1000 * 1000
        };

        nanosleep(&sleep_time, NULL);
    }
}

int main(void) {
    if (apply_crop_index(0) != 0) {
        fprintf(stderr, "Unable to prime Majestic configuration.\n");
    }

    // Stay alive even if the Matek link is missing or drops later by retrying forever.
    while (1) {
        const int matek_fd = open_matek_device();

        if (matek_fd < 0) {
            fprintf(stderr, "Matek device unavailable; retrying...\n");
            nanosleep(&RECONNECT_DELAY, NULL);
            continue;
        }

        event_loop(matek_fd);
        fprintf(stderr, "Matek loop exited; reconnecting...\n");
        close(matek_fd);
        nanosleep(&RECONNECT_DELAY, NULL);
    }
}
