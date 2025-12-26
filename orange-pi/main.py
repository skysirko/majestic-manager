from pymavlink import mavutil
import time

# Baud rate of the UART between RunCam (OpenIPC) and the Matek FC.
# Must match SERIALx_BAUD on the ArduPilot side.
BAUD = 57600

# Linux device for the Matek flight controller.
# On OpenIPC this is usually a USB gadget or UART exposed as /dev/ttyS* or /dev/serial/by-id/*
# Using by-id is good: stable across reboots.
DEVICE = "/dev/serial/by-id/usb-ArduPilot_MatekH743_1F002D000D51333230363937-if00"

# MAVLink system ID for this node.
# 1 is the autopilot (reserved).
# 2 is conventionally the first companion computer on the vehicle.
# Must be unique on the MAVLink network.
SYSTEM_SOURCE = 2

def main():
    connection = mavutil.mavlink_connection(
        device=DEVICE,
        baud=BAUD,

        # Who we are (system identity).
        # This identifies this script as a separate MAVLink system running on the UAV.
        source_system=SYSTEM_SOURCE,

        # What we are (component role).
        # ONBOARD_COMPUTER (191) is correct for a payload / companion device
        # running on the vehicle (RunCam + OpenIPC).
        source_component=mavutil.mavlink.MAV_COMP_ID_ONBOARD_COMPUTER
    )

    # Blocks until we see the FC heartbeat.
    # This confirms link is working
    print("waiting for heartbeat from autopilot...")
    connection.wait_heartbeat()
    print("!!! heartbeat received !!!")

    last = 0
    while True:
        now = time.time()
        if now - last >= 1.0:
            connection.mav.heartbeat_send(
                # Declares this component as an onboard controller / companion.
                # This affects how GCS tools categorize us.
                mavutil.mavlink.MAV_TYPE_ONBOARD_CONTROLLER,

                # Explicitly states: "I am NOT an autopilot".
                # Prevents GCS / FC confusion and avoids double-autopilot scenarios.
                mavutil.mavlink.MAV_AUTOPILOT_INVALID,

                0,  # base_mode
                0,  # custom_mode
                0   # system_status
            )
            # Sending heartbeats is REQUIRED so ArduPilot:
            #   - knows we exist
            #   - keeps routing STATUSTEXT and other broadcasts to this link

            last = now

        msg = connection.recv_match(type="STATUSTEXT", blocking=False)

        if msg:
            print(msg.text)

        # Small sleep to avoid busy-looping the CPU.
        time.sleep(0.01)

if __name__ == "__main__":
    main()
