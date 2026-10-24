# Moniteur serie simple : affiche les lignes horodatees et peut envoyer
# des commandes (--cmd) ou surveiller un fichier de commande (--cmdfile,
# utile pour piloter depuis un autre terminal).
import argparse
import datetime
import os
import serial
import sys
import time

parser = argparse.ArgumentParser()
parser.add_argument('--port', default='COM10')
parser.add_argument('--minutes', type=float, default=0, help='0 = illimite')
parser.add_argument('--cmd', action='append', default=[])
parser.add_argument('--cmdfile', default=None)
args = parser.parse_args()

port = serial.Serial(args.port, 115200, timeout=0.1)
ts = lambda: datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]

time.sleep(1)
port.reset_input_buffer()
for cmd in args.cmd:
    port.write((cmd + "\n").encode())
    print(f">>> sent: {cmd}", flush=True)
print("READY", flush=True)
end = time.monotonic() + args.minutes * 60 if args.minutes else None
while end is None or time.monotonic() < end:
    try:
        if args.cmdfile and os.path.exists(args.cmdfile):
            cmd = open(args.cmdfile).read().strip()
            os.remove(args.cmdfile)
            if cmd:
                port.write((cmd + "\n").encode())
                print(f">>> sent: {cmd}", flush=True)
        line = port.readline().decode(errors="replace").rstrip()
        if line:
            print(f"{ts()} {line}", flush=True)
    except (serial.SerialException, OSError) as e:
        print("ERR", e, flush=True)
        break
port.close()
