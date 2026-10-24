# Surveillance serie prolongee : journal horodate dans ../logs/,
# reouverture automatique du port, requetes bootlog/status sans commande RF.
import argparse
from datetime import datetime
from pathlib import Path
import time
import serial

parser = argparse.ArgumentParser()
parser.add_argument('--port', default='COM10')
parser.add_argument('--minutes', type=float, default=30)
args = parser.parse_args()
logdir = Path(__file__).resolve().parents[1] / 'logs'
logdir.mkdir(exist_ok=True)
path = logdir / ('fan_watch_' + datetime.now().strftime('%Y%m%d_%H%M%S') + '.log')
end = time.monotonic() + args.minutes * 60
with path.open('x', encoding='utf-8', buffering=1) as log:
    def record(message):
        line = datetime.now().astimezone().isoformat(timespec='seconds') + ' ' + message
        log.write(line + '\n')
        print(line, flush=True)
    record('WATCH START ' + str(path))
    port = None
    last_rx = last_query = 0
    try:
        while time.monotonic() < end:
            if port is None:
                try:
                    port = serial.Serial(args.port, 115200, timeout=0.5, write_timeout=1)
                    record('PORT OPEN ' + args.port)
                    port.write(b'bootlog\nstatus\n')
                    last_rx = last_query = time.monotonic()
                except serial.SerialException as error:
                    record('OPEN FAILED ' + str(error))
                    time.sleep(3)
                    continue
            try:
                raw = port.readline()
                if raw:
                    last_rx = time.monotonic()
                    record(raw.decode('utf-8', errors='replace').rstrip())
                now = time.monotonic()
                if now - last_query > 60:
                    port.write(b'bootlog\nstatus\n')
                    last_query = now
                if now - last_rx > 15 and now - last_query > 15:
                    record('NO DATA >15s: requesting bootlog/status (no fan command)')
                    port.write(b'bootlog\nstatus\n')
                    last_query = now
            except (serial.SerialException, OSError) as error:
                record('PORT LOST ' + str(error))
                port.close()
                port = None
                time.sleep(2)
    finally:
        if port is not None:
            port.close()
        record('WATCH END')
