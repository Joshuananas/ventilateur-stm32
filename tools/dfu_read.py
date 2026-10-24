# Lecture de la flash interne du STM32 en mode DFU (sans debugger).
# Dependances : pyusb, libusb_package. Carte en mode DFU (VID:PID 0483:DF11).
# Usage : python tools/dfu_read.py [sortie.bin] [adresse] [taille]
import sys, struct, time
import usb.core, usb.util, usb.backend.libusb1
import libusb_package

be = usb.backend.libusb1.get_backend(find_library=libusb_package.find_library)
dev = usb.core.find(idVendor=0x0483, idProduct=0xDF11, backend=be)
if dev is None:
    print("DFU device not found"); sys.exit(1)

dev.set_configuration()
cfg = dev.get_active_configuration()
intf = None
for i in cfg:
    if i.bInterfaceClass == 0xFE:  # Application Specific / DFU
        intf = i; break
if intf is None:
    intf = cfg[(0,0)]
usb.util.claim_interface(dev, intf.bInterfaceNumber)
print("Interface", intf.bInterfaceNumber, "alt", intf.bAlternateSetting)

DFU_DNLOAD=1; DFU_UPLOAD=2; DFU_GETSTATUS=3; DFU_CLRSTATUS=4; DFU_ABORT=6
REQ_OUT = 0x21; REQ_IN = 0xA1
IFACE = intf.bInterfaceNumber

def getstatus():
    d = dev.ctrl_transfer(REQ_IN, DFU_GETSTATUS, 0, IFACE, 6, timeout=5000)
    return d[0], d[4]  # status, state

def wait_idle():
    for _ in range(200):
        st, state = getstatus()
        if state in (2, 5, 9):  # dfuIDLE, dnload-idle, upload-idle
            return
        if st != 0:
            dev.ctrl_transfer(REQ_OUT, DFU_CLRSTATUS, 0, IFACE)
        time.sleep(0.005)
    raise RuntimeError("not idle, state=%d" % state)

def dnload(block, data):
    dev.ctrl_transfer(REQ_OUT, DFU_DNLOAD, block, IFACE, bytes(data), timeout=5000)

def upload(block, length):
    return dev.ctrl_transfer(REQ_IN, DFU_UPLOAD, block, IFACE, length, timeout=5000)

def set_address(addr):
    dnload(0, [0x41] + list(struct.pack("<I", addr)))
    wait_idle()

OUT = sys.argv[1] if len(sys.argv) > 1 else "flash.bin"
ADDR = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x08000000
SIZE = int(sys.argv[3], 0) if len(sys.argv) > 3 else 512 * 1024
BLOCK = 2048

# ensure DFU idle
try:
    wait_idle()
except RuntimeError:
    dev.ctrl_transfer(REQ_OUT, DFU_ABORT, 0, IFACE)
    wait_idle()

data = bytearray()
a = ADDR
while a < ADDR + SIZE:
    dev.ctrl_transfer(REQ_OUT, DFU_ABORT, 0, IFACE)
    wait_idle()
    set_address(a)
    wait_idle()
    got = 0
    blk = 0
    chunk = bytearray()
    while got < BLOCK:
        try:
            d = upload(blk, BLOCK - got)
        except Exception as e:
            st, state = getstatus()
            print("\nupload err @blk", blk, e, "status", st, "state", state)
            raise
        if len(d) == 0:
            break
        chunk += d
        got += len(d)
        blk += 1
    data += chunk
    a += BLOCK
    print("\r0x%08X" % a, end="", flush=True)
print()
open(OUT, "wb").write(bytes(data[:SIZE]))
print("Wrote", OUT, len(data[:SIZE]), "bytes")
