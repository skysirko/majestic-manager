#ifndef MAVLINK_H
#define MAVLINK_H

#include <stdbool.h>
#include <stdint.h>
#include <termios.h>

#define MAVLINK_V2_STX ((uint8_t)0xFD)
#define MAVLINK_V2_HEADER_LEN 9
#define MAVLINK_SIGNATURE_LEN 13  /* bytes appended when a MAVLink v2 frame is signed */
#define MATEK_DEVICE "/dev/ttyS2"
#define SERIAL_SPEED B57600
#define MAVLINK_STATUSTEXT_MAX_LEN 50
#define MAVLINK_SYSTEM_ID 2
#define MAVLINK_COMPONENT_ID 191
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

struct mavlink_statustext {
    uint8_t severity;
    uint16_t text_id;
    uint8_t chunk_seq;
    char text[MAVLINK_STATUSTEXT_MAX_LEN + 1];
};

bool mavlink_init(void);
void mavlink_close(void);

/* Emits a heartbeat frame using MAVLink v2 framing */
bool mavlink_send_heartbeat(void);

/* Blocking helpers that hide parser internals. Return 1 on success, 0 on timeout, -1 on error. */
int mavlink_wait_heartbeat(int timeout_ms);
int mavlink_read_statustext(int timeout_ms, struct mavlink_statustext *out_msg);

#endif /* MAVLINK_H */
