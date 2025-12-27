from pymavlink import mavutil
import time
import glob
import os
import subprocess

MATEK_FOLDER_PATH = "/dev/serial/by-id"
MAJESTIC_CONFIG_PATH = os.environ.get("MAJESTIC_CONFIG_PATH", "/etc/majestic.yaml")

# Baud rate of the UART between RunCam (OpenIPC) and the Matek FC.
# Must match SERIALx_BAUD on the ArduPilot side.
BAUD = 57600

# MAVLink system ID for this node.
# 1 is the autopilot (reserved).
# 2 is conventionally the first companion computer on the vehicle.
# Must be unique on the MAVLink network.
SYSTEM_SOURCE = 2

CROPS = [
    "0x0x3840x2160",
    "320x180x3520x1980",
    "640x360x3200x1800"
    "960x540x2880x1620"
]
CROP_INDEX_MIN = 0
CROP_INDEX_MAX = len(CROPS)
CROP_INDEX_CURRENT = CROP_INDEX_MIN


def _reload_majestic():
    """Ask Majestic to reload its configuration so the new crop takes effect."""
    for command in (["killall", "-1", "majestic"], ["killall", "majestic"]):
        try:
            subprocess.run(
                command,
                check=True,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL
            )
            return
        except FileNotFoundError:
            print("killall command not available; unable to signal Majestic.")
            return
        except subprocess.CalledProcessError:
            # Try the next option (plain killall) before giving up.
            continue

    print("Unable to signal Majestic; crop change may require manual restart.")


"""Update the crop entry under video0 inside the Majestic configuration."""
def set_crop_in_config(crop: str, ensure_exists: bool = False, config_path: str = MAJESTIC_CONFIG_PATH) -> None:
    try:
        with open(config_path, "r", encoding="utf-8") as config_file:
            lines = config_file.readlines()
    except FileNotFoundError:
        print(f"Majestic config not found at {config_path}; skipping crop update.")
        return

    in_video0 = False
    updated = False
    section_indent = ""
    insert_index = None

    for index, line in enumerate(lines):
        stripped = line.strip()

        if stripped.startswith("video0:"):
            in_video0 = True
            section_indent = line[: len(line) - len(line.lstrip())]
            insert_index = index + 1
            continue

        # Leaving the video0 section once indentation drops back to column 0.
        if in_video0 and line and not line[0].isspace():
            break

        if in_video0 and stripped.startswith("crop:"):
            indent = line[: len(line) - len(line.lstrip())]
            lines[index] = f"{indent}crop: {crop}\n"
            updated = True
            break

    if not updated:
        if ensure_exists and insert_index is not None:
            indent = section_indent + "  "
            lines.insert(insert_index, f"{indent}crop: {crop}\n")
            updated = True
        else:
            print("crop entry inside video0 not found; no changes written.")
            return

    with open(config_path, "w", encoding="utf-8") as config_file:
        config_file.writelines(lines)

    _reload_majestic()


def connect_to_matek():
    files = glob.glob(os.path.join(MATEK_FOLDER_PATH, "*"))
    files.sort()

    for filepath in files:
        try:
            return mavutil.mavlink_connection(
                device=filepath,
                baud=BAUD,

                # Who we are (system identity).
                # This identifies this script as a separate MAVLink system running on the UAV.
                source_system=SYSTEM_SOURCE,

                # What we are (component role).
                # ONBOARD_COMPUTER (191) is correct for a payload / companion device
                # running on the vehicle (RunCam + OpenIPC).
                source_component=mavutil.mavlink.MAV_COMP_ID_ONBOARD_COMPUTER
            )
        except Exception as exception:
            print(exception)

    raise Exception("Matek not found.")

def setup():
    crop = CROPS[CROP_INDEX_CURRENT]
    set_crop_in_config(crop, ensure_exists=True)
    return

def execute(command: str) -> None:
    global CROP_INDEX_CURRENT

    if command == "zoom_in" and CROP_INDEX_CURRENT < CROP_INDEX_MAX:
        CROP_INDEX_CURRENT += 1
        crop = CROPS[CROP_INDEX_CURRENT]

        set_crop_in_config(crop)
        return

    if command == "zoom_out" and CROP_INDEX_CURRENT > CROP_INDEX_MIN:
        CROP_INDEX_CURRENT -= 1
        crop = CROPS[CROP_INDEX_CURRENT]

        set_crop_in_config(crop)
        return


def main():
    connection = connect_to_matek()

    # Blocks until we see the FC heartbeat.
    # This confirms link is working
    print("waiting for heartbeat from autopilot...")
    connection.wait_heartbeat()
    print("!!! heartbeat received !!!")

    setup()

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

        if msg and msg.text in ["zoom_in", "zoom_out"]:
            execute(msg.text)

        # Small sleep to avoid busy-looping the CPU.
        time.sleep(0.01)

if __name__ == "__main__":
    main()
