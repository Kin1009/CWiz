"""run_sim.py - chay chuong trinh Wiz trong mo phong va in man hinh LCD ra terminal.

  python3 run_sim.py prog.wz                       chay 3 trieu lenh, in man hinh (nua do phan giai)
  python3 run_sim.py prog.wz --steps 20000000      chay lau hon (game can nhieu khung hinh)
  python3 run_sim.py prog.wz --keys K_UP,K_OK      giu cac phim nay suot luc chay
  python3 run_sim.py prog.wz --full                in du 192x64 (moi diem 1 ky tu)
  python3 run_sim.py prog.wz --press K_OK@2000000  nhan K_OK bat dau tu lenh thu 2.000.000 (nhieu: dau phay)
"""
import argparse
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from build import build_source
from u8sim import U8
from wizc import key_consts, WizError
from u8asm import AsmError


def pair(name, target):
    table = key_consts(target or 'cwii')
    if name not in table or table[name] < 0:
        sys.exit('khong co phim ten %r tren may %s (xem K_* trong GUIDE muc 12)' % (name, target or 'cwii'))
    k = table[name]
    return (0x80 >> (k >> 3), 0x80 >> (k & 7))


ap = argparse.ArgumentParser(description='Chay chuong trinh Wiz trong mo phong')
ap.add_argument('prog')
ap.add_argument('--steps', type=int, default=3_000_000)
ap.add_argument('--keys', default='')
ap.add_argument('--press', default='')
ap.add_argument('--full', action='store_true')
ap.add_argument('--target', default=None, help='cwii (default) or cw')
a = ap.parse_args()

try:
    rom, full, vars_, syms = build_source(open(a.prog).read(), a.target)
except (WizError, AsmError) as e:
    sys.exit('error: %s' % e)
start, stop = None, ()
if a.target == 'cwapp':     # program patched into the firmware: begin at its entry in segment 3, stop when it returns to the firmware reset handler
    start, stop = (syms['_APP'] >> 16, syms['_APP'] & 0xFFFF), {(0, 0xA1CE)}
m = U8(rom, [pair(k, a.target) for k in a.keys.split(',') if k], a.target, start, stop)
events = []
for item in a.press.split(','):
    if item:
        name, at = item.split('@')
        events.append((int(at), pair(name, a.target)))
events.sort()
why = ''
if not events:
    why = m.run(a.steps)
else:
    for at, p in events:
        why = m.run(at)
        m.keys = m.keys + [p]
    why = m.run(a.steps)
print('stopped:', why, '| steps', m.steps)
art = m.ascii().split('\n')
print('\n'.join(art if a.full else [l[::2] for l in art[::2]]))
