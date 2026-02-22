#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "majestic_config.h"
#include "mavlink.h"

static const char *const CROPS[] = {
    "0x0x1920x1080",
    "480x270x960x540",
    "720x405x480x270",
    "840x472x240x135",
};

static size_t crop_index = 0;

static double monotonic_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void handle_statustext(const struct mavlink_statustext *msg) {
    printf("[STATUSTEXT] `%s`\n", msg->text);
    if (msg->chunk_seq != 0) {
        return;
    }

    if (strcmp(msg->text, "zoom_in") == 0 && crop_index + 1 < ARRAY_SIZE(CROPS)) {
        crop_index++;
        majestic_config_set_crop(CROPS[crop_index]);
        return;
    }

    if (strcmp(msg->text, "zoom_out") == 0 && crop_index > 0) {
        crop_index--;
        majestic_config_set_crop(CROPS[crop_index]);
        return;
    }
}

static void event_loop(void) {
    double last_heartbeat = 0.0;
    while (true) {
        double now = monotonic_seconds();
        if (now - last_heartbeat >= 1.0) {
            mavlink_send_heartbeat();
            last_heartbeat = now;
        }
        struct mavlink_statustext msg;
        int status = mavlink_read_statustext(100, &msg);
        if (status < 0) {
            break;
        }
        if (status > 0) {
            handle_statustext(&msg);
        }
    }
}

int main(void) {
    if (!majestic_config_init()) {
        return 1;
    }
    // set no zoom on application bootstrap
    const char *crop = CROPS[crop_index];
    majestic_config_set_crop(crop);

    if (!mavlink_init()) {
        majestic_config_free();
        return 1;
    }
    event_loop();
    mavlink_close();
    majestic_config_free();
    return 0;
}
