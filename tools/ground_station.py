#!/usr/bin/env python3
"""Bodenstation: empfaengt die Sensorwerte vom Waveshare USB-TO-LoRa-HF,
speichert sie als CSV und zeigt sie live in Diagrammen an.

Erwartetes Format pro Zeile (vom Teensy):
    Zaehler,ms_seit_start,Temp_C,Druck_hPa,Feuchte_%,Hoehe_m

Benutzung:
    python3 tools/ground_station.py                      # Port automatisch, Live-Plot
    python3 tools/ground_station.py -p /dev/cu.usbmodemXXXX
    python3 tools/ground_station.py --replay data/flug_2026-11-19_16-30-00.csv
    python3 tools/ground_station.py --no-plot            # nur speichern (Terminal)
    python3 tools/ground_station.py --qnh 985            # QNH an den Teensy schicken

--qnh: "relativer Luftdruck (NHN)" vom Wetterbericht in hPa. Wird beim Start per
Funk an den Teensy geschickt (wiederholt, bis er mit ACK bestaetigt) und dort
fuer die Hoehenberechnung verwendet und im EEPROM gespeichert.

Daten landen in data/flug_<datum>_<uhrzeit>.csv. Fenster schliessen oder Ctrl+C beendet.
"""
import argparse
import csv
import datetime
import math
import os
import queue
import sys
import threading
import time

import serial
from serial.tools import list_ports

FIELDS = ["empfangen", "zaehler", "ms", "temp_c", "druck_hpa", "feuchte", "hoehe_m"]
WAVESHARE_VID = 0x1A86  # WCH CH343 auf dem Waveshare-Stick
DATA_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data")


def find_port():
    ports = list(list_ports.comports())
    for p in ports:
        if p.vid == WAVESHARE_VID:
            return p.device
    usb = [p.device for p in ports if "usb" in p.device.lower()]
    return usb[0] if usb else None


def to_float(s):
    try:
        return float(s) if s.strip() else math.nan
    except ValueError:
        return math.nan


def parse_line(line):
    """'12,24000,21.53,1012.84,45.2,3.4' -> dict, oder None bei ungueltiger Zeile."""
    parts = line.strip().split(",")
    if len(parts) < 3 or not parts[0].isdigit():
        return None
    if parts[2] == "NOSENSOR":
        return {"zaehler": int(parts[0]), "ms": int(parts[1]), "temp_c": math.nan,
                "druck_hpa": math.nan, "feuchte": math.nan, "hoehe_m": math.nan, "nosensor": True}
    if len(parts) != 6:
        return None
    try:
        rec = {"zaehler": int(parts[0]), "ms": int(parts[1]),
               "temp_c": to_float(parts[2]), "druck_hpa": to_float(parts[3]),
               "feuchte": to_float(parts[4]), "hoehe_m": to_float(parts[5])}
    except ValueError:
        return None
    # Plausibilitaetscheck gegen beschaedigte/zusammengeklebte Zeilen
    if not (-40 <= rec["temp_c"] <= 85 and 300 <= rec["druck_hpa"] <= 1100
            and -1000 <= rec["hoehe_m"] <= 40000):
        return None
    if not math.isnan(rec["feuchte"]) and not 0 <= rec["feuchte"] <= 100:
        return None
    return rec


def serial_reader(ser, out_q, stop):
    # Der Stick puffert Pakete, die ankamen, als niemand den Port offen hatte.
    # Diese kommen beim Oeffnen gesammelt (und teils zerstueckelt) an -> verwerfen.
    end = time.time() + 1.0
    while time.time() < end:
        ser.read(4096)
    buf = b""
    try:
        while not stop.is_set():
            buf += ser.read(256)
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                out_q.put(raw.decode(errors="replace").strip())
    finally:
        ser.close()


class Store:
    def __init__(self):
        self.t, self.temp, self.press, self.hum, self.alt = [], [], [], [], []
        self.received = 0
        self.lost = 0
        self.restarts = 0
        self.last_counter = None
        self.t0 = None

    def add(self, rec, ts):
        c = rec["zaehler"]
        if self.last_counter is not None and c > self.last_counter + 1:
            self.lost += c - self.last_counter - 1
        if self.last_counter is not None and c < self.last_counter:
            self.restarts += 1  # Teensy neu gestartet (Zaehler wieder bei 0)
        self.last_counter = c
        self.received += 1
        if self.t0 is None:
            self.t0 = ts
        self.t.append(ts - self.t0)
        self.temp.append(rec["temp_c"])
        self.press.append(rec["druck_hpa"])
        self.hum.append(rec["feuchte"])
        self.alt.append(rec["hoehe_m"])


def open_csv():
    os.makedirs(DATA_DIR, exist_ok=True)
    name = datetime.datetime.now().strftime("flug_%Y-%m-%d_%H-%M-%S.csv")
    path = os.path.abspath(os.path.join(DATA_DIR, name))
    f = open(path, "w", newline="")
    w = csv.writer(f)
    w.writerow(FIELDS)
    f.flush()
    return path, f, w


def load_replay(path, store):
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            rec = {k: to_float(row[k]) for k in ("temp_c", "druck_hpa", "feuchte", "hoehe_m")}
            rec["zaehler"] = int(row["zaehler"])
            rec["ms"] = int(row["ms"])
            ts = datetime.datetime.fromisoformat(row["empfangen"]).timestamp()
            store.add(rec, ts)


def make_plot(store, title):
    import matplotlib.pyplot as plt

    fig, axes = plt.subplots(4, 1, sharex=True, figsize=(10, 9))
    fig.canvas.manager.set_window_title(title)
    specs = [("Temperatur [°C]", "temp", "tab:red"),
             ("Luftdruck [hPa]", "press", "tab:blue"),
             ("Höhe [m]", "alt", "tab:green"),
             ("Luftfeuchte [%]", "hum", "tab:purple")]
    lines = []
    for ax, (label, attr, color) in zip(axes, specs):
        (ln,) = ax.plot([], [], color=color, marker=".", markersize=3, linewidth=1)
        ax.set_ylabel(label)
        ax.grid(True, alpha=0.3)
        ax.ticklabel_format(axis="y", useOffset=False, style="plain")
        lines.append((ax, ln, attr))
    axes[-1].set_xlabel("Zeit seit Aufnahmestart [s]")
    status = fig.text(0.01, 0.995, "", va="top", fontsize=9, family="monospace")
    fig.tight_layout(rect=(0, 0, 1, 0.97))

    def redraw():
        for ax, ln, attr in lines:
            ys = getattr(store, attr)
            ln.set_data(store.t, ys)
            valid = [y for y in ys if not math.isnan(y)]
            if attr == "hum" and not valid and store.t:
                ax.set_title("kein Feuchtesensor (BMP280)", fontsize=8, loc="right", color="gray")
            ax.relim()
            ax.autoscale_view()
        last = ""
        if store.t:
            last = (f"  |  T {store.temp[-1]:.2f} °C  p {store.press[-1]:.2f} hPa  "
                    f"h {store.alt[-1]:.1f} m")
        status.set_text(f"Pakete: {store.received}  verloren: {store.lost}  "
                        f"Neustarts: {store.restarts}{last}")
        fig.canvas.draw_idle()

    return plt, fig, redraw


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-p", "--port", help="serieller Port des Waveshare-Sticks")
    ap.add_argument("-b", "--baud", type=int, default=115200)
    ap.add_argument("--replay", help="gespeicherte CSV-Datei anzeigen statt live empfangen")
    ap.add_argument("--no-plot", action="store_true", help="nur speichern und im Terminal ausgeben")
    ap.add_argument("--qnh", type=float, help="Luftdruck auf Meereshoehe (hPa), wird an den Teensy geschickt")
    args = ap.parse_args()

    store = Store()

    if args.replay:
        load_replay(args.replay, store)
        plt, fig, redraw = make_plot(store, os.path.basename(args.replay))
        redraw()
        plt.show()
        return

    port = args.port or find_port()
    if not port:
        sys.exit("Waveshare-Stick nicht gefunden. Eingesteckt? Mit -p den Port angeben.")

    csv_path, csv_file, writer = open_csv()
    print(f"Empfange von {port} @ {args.baud} Baud")
    print(f"Speichere nach {csv_path}")

    if args.qnh is not None and not 900 <= args.qnh <= 1100:
        sys.exit("--qnh muss zwischen 900 und 1100 hPa liegen")

    ser = serial.Serial(port, args.baud, timeout=0.2)
    ser.reset_input_buffer()  # alte, gepufferte Pakete verwerfen
    q = queue.Queue()
    stop = threading.Event()
    threading.Thread(target=serial_reader, args=(ser, q, stop), daemon=True).start()

    # QNH senden und wiederholen, bis der Teensy bestaetigt
    qnh = {"pending": args.qnh is not None, "tries": 0, "next": 0.0}
    QNH_MAX_TRIES, QNH_RETRY_S = 8, 2.5

    def send_qnh():
        if not qnh["pending"] or time.time() < qnh["next"]:
            return
        if qnh["tries"] >= QNH_MAX_TRIES:
            print(f"[QNH] keine Bestaetigung nach {QNH_MAX_TRIES} Versuchen - Teensy an?", flush=True)
            qnh["pending"] = False
            return
        qnh["tries"] += 1
        qnh["next"] = time.time() + QNH_RETRY_S
        ser.write(f"QNH={args.qnh:.2f}\r\n".encode())
        print(f"[QNH] sende {args.qnh:.2f} hPa (Versuch {qnh['tries']})", flush=True)

    def drain():
        while True:
            try:
                line = q.get_nowait()
            except queue.Empty:
                return
            if line.startswith("ACK,QNH,"):
                qnh["pending"] = False
                print(f"[QNH] Teensy bestaetigt: {line.split(',')[2]} hPa", flush=True)
                continue
            rec = parse_line(line)
            if rec is None:
                if line:
                    print(f"[verworfen] {line}", flush=True)
                continue
            now = datetime.datetime.now()
            store.add(rec, now.timestamp())
            writer.writerow([now.isoformat(timespec="milliseconds"), rec["zaehler"], rec["ms"],
                             *("" if math.isnan(rec[k]) else rec[k]
                               for k in ("temp_c", "druck_hpa", "feuchte", "hoehe_m"))])
            csv_file.flush()
            if rec.get("nosensor"):
                print(f"#{rec['zaehler']:>5}  KEIN SENSOR am Teensy", flush=True)
            else:
                hum = "" if math.isnan(rec["feuchte"]) else f"  {rec['feuchte']:5.1f} %"
                print(f"#{rec['zaehler']:>5}  {rec['temp_c']:6.2f} °C  {rec['druck_hpa']:8.2f} hPa"
                      f"  {rec['hoehe_m']:7.1f} m{hum}", flush=True)

    try:
        if args.no_plot:
            while True:
                send_qnh()
                drain()
                time.sleep(0.1)
        else:
            plt, fig, redraw = make_plot(store, "LoRa Bodenstation")
            plt.ion()
            plt.show(block=False)
            while plt.fignum_exists(fig.number):
                send_qnh()
                drain()
                redraw()
                plt.pause(0.25)
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        time.sleep(0.3)
        ser.close()
        csv_file.close()
        print(f"\n{store.received} Pakete gespeichert in {csv_path}")


if __name__ == "__main__":
    main()
