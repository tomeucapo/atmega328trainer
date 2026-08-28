#!/usr/bin/env python3

import re
import time
import serial

PORT = "/dev/ttyUSB0"

ser = serial.Serial(
    port=PORT,
    baudrate=9600,
    bytesize=serial.EIGHTBITS,
    parity=serial.PARITY_NONE,
    stopbits=serial.STOPBITS_ONE,
    timeout=2,
    xonxoff=False,
    rtscts=False,
    dsrdtr=False
)


def send_command(cmd):
    """Envía un comando y devuelve las líneas recibidas."""

    ser.reset_input_buffer()
    ser.write((cmd + "\r\n").encode())

    lines = []

    while True:
        line = ser.readline().decode("ascii", errors="ignore").strip()

        if not line:
            break

        lines.append(line)

    return lines


def get_anemo():
    lines = send_command("@STAT_ANEMO")

    for line in lines:
        m = re.search(r"ANEMO=([0-9.]+)", line)
        if m:
            return float(m.group(1))

    return 0


def get_rain():
    lines = send_command("@STAT_RAIN")

    data = {
        "count": 0,
        "mm": 0.0,
        "lm2": 0.0
    }

    for line in lines:

        if line.startswith("RAIN_COUNT="):
            data["count"] = int(line.split("=")[1])

        elif line.startswith("RAIN_MM="):
            data["mm"] = float(line.split("=")[1])

        elif line.startswith("RAIN_LM2="):
            data["lm2"] = float(line.split("=")[1])

    return data


try:
    send_command("@RESET_RAIN")

    while True:

        viento = get_anemo()
        lluvia = get_rain()

        ts = time.strftime("%Y-%m-%d %H:%M:%S")
        print(f"{ts}\t{viento}\t{lluvia['lm2']}")

        #print(f"Viento : {viento} km/h")
        #print(f"Lluvia : {lluvia['count']} pulsos")
        #print(f"Rain mm: {lluvia['mm']}")
        #print(f"Rain L/m²: {lluvia['lm2']}")

        time.sleep(2)

except KeyboardInterrupt:
    print("\nFinalizando...")

finally:
    ser.close()