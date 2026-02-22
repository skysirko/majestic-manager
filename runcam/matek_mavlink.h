#pragma once

#include <stdint.h>

#define MATEK_STATUSTEXT_MAX_LEN 50

typedef struct matek_statustext {
    uint8_t severity;
    uint16_t id;
    uint8_t chunk_seq;
    char text[MATEK_STATUSTEXT_MAX_LEN + 1]; // +1 to append '\0' after copying MAVLink's payload
} matek_statustext_t;

int open_matek_device(void);
int send_heartbeat(int fd);
int receive_statustext(int fd, matek_statustext_t *message);
