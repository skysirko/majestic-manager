import clr

clr.AddReference('MAVLink')
clr.AddReference('System')
import MAVLink
from System import *


with IO.Ports.SerialPort('COM1') as serialPort:
    serialPort.BaudRate = 9600
    serialPort.Open()

    while True:
        line = serialPort.ReadLine().strip()

        if line == "main_res_up":
            line = "day_mode"
        elif line == "main_res_down":
            line = "night_mode"

        print(line)

        MAV.send_text(0, line)
