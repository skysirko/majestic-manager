#ifndef MAVLINK_PROTO_H
#define MAVLINK_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAVLINK_V2_STX ((uint8_t)0xFD)
#define MAVLINK_V1_STX ((uint8_t)0xFE)
#define MAVLINK_SIGNATURE_LEN 13  /* bytes appended when a MAVLink v2 frame is signed */

enum {
    MAVLINK_MSG_ID_HEARTBEAT = 0,
    MAVLINK_MSG_ID_STATUSTEXT = 253,
};

/* Accumulates MAVLink CRC over an arbitrary buffer */
uint16_t mavlink_crc_accumulate_buffer(const uint8_t *buf, size_t len, uint16_t crc);
/* Emits a heartbeat frame using MAVLink v2 framing */
bool mavlink_send_heartbeat(int fd,
                            uint8_t seq,
                            uint8_t system_id,
                            uint8_t component_id);

#endif /* MAVLINK_PROTO_H */
