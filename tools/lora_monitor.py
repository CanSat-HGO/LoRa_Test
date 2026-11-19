#!/usr/bin/env python3
"""Serieller Monitor fuer den Waveshare USB-TO-LoRa-HF.

Zeigt empfangene Daten als Text + Hex an (mit Zeitstempel) und schickt
alles, was du eintippst, an den Stick (z.B. AT-Befehle oder Testnachrichten).

Benutzung:
    pip install -r tools/requirements.txt
    python3 tools/lora_monitor.py                 # Port automatisch suchen
    python3 tools/lora_monitor.py -p /dev/tty.usbserial-XXXX -b 115200
    python3 tools/lora_monitor.py --log empfang.csv

Beenden mit Ctrl+C.
"""
import argparse
import datetime
import sys
import threading

import serial
from serial.tools import list_ports


def find_port():
    candidates = [p.device for p in list_ports.comports()
                  if "usb" in p.device.lower() and not p.device.startswith("/dev/cu.Bluetooth")]
    # auf macOS lieber tty.* als cu.* zeigen, beide funktionieren
    if not candidates:
        return None
    if len(candidates) > 1:
        print("Mehrere Ports gefunden:", ", ".join(candidates))
        print(f"Nehme {candidates[0]} (mit -p anderen waehlen)")
    return candidates[0]


def fmt(data: bytes) -> str:
    ascii_ = "".join(chr(b) if 32 <= b < 127 else "." for b in data)
    hex_ = " ".join(f"{b:02X}" for b in data)
    return f"{ascii_!r:<40}  | {hex_}"


def reader(ser, log):
    while True:
        try:
            data = ser.read(ser.in_waiting or 1)
        except serial.SerialException as e:
            print(f"\n[Fehler] {e}")
            return
        if not data:
            continue
        # kurz auf Rest des Pakets warten
        ser.timeout = 0.05
        while True:
            more = ser.read(256)
            if not more:
                break
            data += more
        ser.timeout = 0.5
        ts = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
        print(f"\r[{ts}] RX {len(data):3d} B: {fmt(data)}\n> ", end="", flush=True)
        if log:
            log.write(f"{ts},{len(data)},{data.hex()}\n")
            log.flush()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-p", "--port", help="serieller Port, z.B. /dev/tty.usbserial-XXXX")
    ap.add_argument("-b", "--baud", type=int, default=115200)
    ap.add_argument("--log", help="CSV-Datei fuer empfangene Pakete")
    ap.add_argument("--eol", choices=["crlf", "lf", "cr", "none"], default="crlf",
                    help="Zeilenende beim Senden (AT-Befehle brauchen meist CRLF)")
    args = ap.parse_args()

    port = args.port or find_port()
    if not port:
        print("Kein USB-Port gefunden. Stick eingesteckt? Treiber installiert? (ls /dev/tty.usb*)")
        sys.exit(1)

    eol = {"crlf": b"\r\n", "lf": b"\n", "cr": b"\r", "none": b""}[args.eol]
    ser = serial.Serial(port, args.baud, timeout=0.5)
    log = open(args.log, "a") if args.log else None
    print(f"Verbunden mit {port} @ {args.baud} Baud. Eingabe + Enter sendet. Ctrl+C beendet.")

    threading.Thread(target=reader, args=(ser, log), daemon=True).start()
    try:
        while True:
            line = input("> ")
            ser.write(line.encode() + eol)
    except (KeyboardInterrupt, EOFError):
        print("\nBeendet.")
    finally:
        ser.close()
        if log:
            log.close()


if __name__ == "__main__":
    main()
