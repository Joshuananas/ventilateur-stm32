# Apprentissage RF d'un slot + dump : envoie learnN, attend la capture,
# puis dumpN et sauvegarde le CSV dans un fichier.
# Usage : python tools/rf_learn_dump.py --slot 14 --out dump14.txt [--port COM10]
import argparse
import serial
import time
import sys

parser = argparse.ArgumentParser()
parser.add_argument('--slot', type=int, required=True)
parser.add_argument('--port', default='COM10')
parser.add_argument('--out', default=None)
args = parser.parse_args()
out = args.out or f'dump{args.slot}.txt'

port = serial.Serial(args.port, 115200, timeout=0.3)
time.sleep(0.2)
port.reset_input_buffer()

port.write(f"learn{args.slot}\n".encode())
print(f">>> learn{args.slot} envoye — APPULE sur la telecommande ~2 s", flush=True)

buf = b""
t0 = time.time()
while time.time() - t0 < 20:
    d = port.read(4096)
    if d:
        buf += d
        s = buf.decode(errors="replace")
        sys.stdout.write(d.decode(errors="replace"))
        sys.stdout.flush()
        if "DUMPEND" in s or "Rien capte" in s or "transitions capturees" in s:
            time.sleep(0.5)
            break

port.write(f"dump{args.slot}\n".encode())
time.sleep(0.1)
t0 = time.time()
while time.time() - t0 < 8:
    d = port.read(8192)
    if d: buf += d
    if b"DUMPEND" in buf: break
open(out, "wb").write(buf)
print(f"\n>>> dump{args.slot} sauvegarde dans {out}", flush=True)
port.close()
