#include "mavlink_proto.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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

/*
 * Straight port of the MAVLink CRC routine; matches the upstream code so our
 * checksums are accepted by autopilots. MAVLink (the drone control protocol)
 * protects each frame with a 16-bit X25 CRC (polynomial 0x1021). Instead of
 * using a lookup table, the reference algorithm applies three XOR/shift
 * operations per byte. Reproducing the same bit fiddling keeps us in sync with
 * the rest of the MAVLink ecosystem. CRC stands for Cyclic Redundancy Check,
 * the standard “parity on steroids” technique for spotting corrupted data.
 */
uint16_t mavlink_crc_accumulate_buffer(const uint8_t *buf, size_t len, uint16_t crc) {
    for (size_t i = 0; i < len; ++i) {
        uint8_t tmp = buf[i] ^ (uint8_t)(crc & 0xFF);
        tmp ^= (tmp << 4);
        crc = (crc >> 8) ^ (tmp << 8) ^ (tmp << 3) ^ (tmp >> 4);
    }
    return crc;
}

/* Serialize and send a heartbeat frame for the provided system/component IDs. */
bool mavlink_send_heartbeat(int fd,
                            uint8_t seq,
                            uint8_t system_id,
                            uint8_t component_id) {
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
    frame[offset++] = seq;
    frame[offset++] = system_id;
    frame[offset++] = component_id;
    frame[offset++] = 0;
    frame[offset++] = 0;
    frame[offset++] = 0;
    memcpy(&frame[offset], payload, sizeof(payload));
    offset += sizeof(payload);

    crc = mavlink_crc_accumulate_buffer(&frame[1], 9, crc);

    crc = mavlink_crc_accumulate_buffer(payload, sizeof(payload), crc);
    uint8_t extra = 50; /* MAVLink heartbeat CRC extra byte */
    crc = mavlink_crc_accumulate_buffer(&extra, 1, crc);
    frame[offset++] = (uint8_t)(crc & 0xFF);
    frame[offset++] = (uint8_t)(crc >> 8);

    return write_all(fd, frame, offset);
}
