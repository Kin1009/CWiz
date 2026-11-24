# RAC getkey test for fx-580VN X.
# Loops forever: read a key with getscancode, print its code, render.
@section.main at 0xd730 backup 0xe9e0

lbl start
    setlr_pc
    setsfr

lbl read_input
    er0 = adr(key)
    getscancode
    setlr_pc
    er0 = eval(adr(key) + dist.main)
    r0 = [er0]
    hex_byte
    ea = eval(adr(code) + dist.main)
    [ea+]=er0
    line_print(0x08, 0x01, eval(adr(line) + dist.main))
    render()
    er14 = eval(adr(restore) - 0x2)
    sp = er14, pop er14

lbl restore
    di, rt
    xr0 = 0xd730, 0xe9e0
    call 09451
    hex fe 01
    er14 = eval(adr(read_input) - 0x2)
    sp = er14, pop er14

lbl line
    "KEY: "
lbl code
    hex 30 30
    "   "
    hex 00

lbl key
    hex 00 00

@section.launcher at 0xd180
hex FD 24
hex 30 30
setlr
xr0 = 0xd730, 0xe9e0
call 09451
hex fe 01
er14 = 0xd72e
sp = er14, pop er14
