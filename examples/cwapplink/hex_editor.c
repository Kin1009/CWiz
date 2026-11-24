#for 580
#cwapp 13 "Hex Editor"
// ============================================================================
// cwapp text grid + hex-editor layout (fx-580VN X, 192 x 64 LCD)
// ============================================================================
// print()/text()/char(): 6 x 8 px cell, 6 px advance.
//   grid: 32 columns x 8 rows
//   column c -> x = c * 6      (c = 0..31, last x = 186)
//   row    r -> y = r * 8      (r = 0..7,  last y = 56)
//   (x, y) is the top-left of the first glyph; print() returns the final x.
//   Past x = 186 or below y = 63 is clipped.
// Layout: 8 bytes per row, 7 data rows (56 visible bytes).
//   row r (0..6):  y = r * 8
//   address:       x = 0          (4 hex chars, 24 px)
//   hex byte n:    x = 30 + n*18  (n = 0..7, "XX " = 18 px)
//   status row:    y = 56, "pos:" + address + pending nibble
// Key scan codes for key() / getkey() / key_down() / key_hit():
//   OPTN = 57
//   0 = 11   1 = 63   2 = 55   3 = 47   4 = 62
//   5 = 54   6 = 46   7 = 61   8 = 53   9 = 45
//   A = 59   (the (-) key)     B = 51   (degrees)
//   C = 43   (x^-1)            D = 35   (sin)
//   E = 27   (cos)             F = 19   (tan)
//   UP = 40   DOWN = 33   LEFT = 41   RIGHT = 32
//   AC = 29   EXE = 15   DEL = 37   MENU = 24
//   key() returns -1 when nothing is pressed.
// A-F are the same physical keys as the app-menu shortcut legends:
// A=(-), B=deg, C=x^-1, D=sin, E=cos, F=tan.
// ============================================================================
char line1[24];
char line2[24];
char line3[24];
char line4[24];
char line5[24];
char line6[24];
char line7[24];
char addr1[6];
char addr2[6];
char addr3[6];
char addr4[6];
char addr5[6];
char addr6[6];
char addr7[6];
char status[5];
char nib[3];
/* Addresses are stored biased: encoded = raw_address XOR 0x8000. */
int addr = -32768;
int selection = 0;
int nibble = -1;
int k;
int lastk = -1;
int v;
char jumpbuf[5];
int jumpto;
int jcount;
int jagain;
int prevSel;
int prevAddr;
int prevNibble;
int wroteFlag;
int fullFlag;
int writtenRow;
int print_ram(int x, int y, char *s) {
    while (*s) {
        glyph(x, y, (int)*s);
        x = x + 6;
        s = s + 1;
    }
    return x;
}
char *int_to_hex(int value, char *out, int digits) {
    if (digits < 1) { digits = 1; }
    if (digits > 4) { digits = 4; }
    int i = 0;
    while (i < digits) {
        int shift = (digits - 1 - i) * 4;
        int n = (value >> shift) & 0xF;
        out[i] = (char)(n < 10 ? 48 + n : 65 + n - 10);
        i = i + 1;
    }
    out[digits] = 0;
    return out;
}
/* Convert between the signed 16-bit storage form and the raw address bits. */
int addrDecode(int encoded) {
    return encoded ^ -32768;
}
void addrToHex(int encoded, char *out) {
    int_to_hex(addrDecode(encoded), out, 4);
}
int hexCharValue(char c) {
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return 0;
}
/* Parse up to four hex digits directly into the biased signed representation.
   This avoids overflowing a signed int while entering addresses >= 0x8000. */
int parseJumpAddress(char *buf, int count) {
    int result = -32768;
    int d;
    if (count > 0) {
        d = hexCharValue(buf[0]);
        if (d < 8) { result = d * 4096 - 32768; }
        else { result = (d - 8) * 4096; }
    }
    if (count > 1) { result = result + hexCharValue(buf[1]) * 256; }
    if (count > 2) { result = result + hexCharValue(buf[2]) * 16; }
    if (count > 3) { result = result + hexCharValue(buf[3]); }
    return result;
}
void readHex(int base, char *buf) {
    int i = 0;
    while (i < 8) {
        int b = peek(addrDecode(base + i));
        int hi = (b >> 4) & 0xF;
        int lo = b & 0xF;
        buf[i * 3 + 0] = (char)(hi < 10 ? 48 + hi : 65 + hi - 10);
        buf[i * 3 + 1] = (char)(lo < 10 ? 48 + lo : 65 + lo - 10);
        buf[i * 3 + 2] = (i == 7) ? 0 : ' ';
        i = i + 1;
    }
    buf[23] = 0;
}
void updateAllLines(int base) {
    readHex(base, line1);
    readHex(base + 8, line2);
    readHex(base + 16, line3);
    readHex(base + 24, line4);
    readHex(base + 32, line5);
    readHex(base + 40, line6);
    readHex(base + 48, line7);
    addrToHex(base, addr1);
    addrToHex(base + 8, addr2);
    addrToHex(base + 16, addr3);
    addrToHex(base + 24, addr4);
    addrToHex(base + 32, addr5);
    addrToHex(base + 40, addr6);
    addrToHex(base + 48, addr7);
}
int keyToHex(int code) {
    if (code == 11) { return 0; }
    if (code == 63) { return 1; }
    if (code == 55) { return 2; }
    if (code == 47) { return 3; }
    if (code == 62) { return 4; }
    if (code == 54) { return 5; }
    if (code == 46) { return 6; }
    if (code == 61) { return 7; }
    if (code == 53) { return 8; }
    if (code == 45) { return 9; }
    if (code == 59) { return 10; }
    if (code == 51) { return 11; }
    if (code == 43) { return 12; }
    if (code == 35) { return 13; }
    if (code == 27) { return 14; }
    if (code == 19) { return 15; }
    return -1;
}
void scrollUp() {
    memcpy(line7, line6, 24);
    memcpy(line6, line5, 24);
    memcpy(line5, line4, 24);
    memcpy(line4, line3, 24);
    memcpy(line3, line2, 24);
    memcpy(line2, line1, 24);
    memcpy(addr7, addr6, 6);
    memcpy(addr6, addr5, 6);
    memcpy(addr5, addr4, 6);
    memcpy(addr4, addr3, 6);
    memcpy(addr3, addr2, 6);
    memcpy(addr2, addr1, 6);
    addr = addr - 8;
    readHex(addr, line1);
    addrToHex(addr, addr1);
}
void scrollDown() {
    memcpy(line1, line2, 24);
    memcpy(line2, line3, 24);
    memcpy(line3, line4, 24);
    memcpy(line4, line5, 24);
    memcpy(line5, line6, 24);
    memcpy(line6, line7, 24);
    memcpy(addr1, addr2, 6);
    memcpy(addr2, addr3, 6);
    memcpy(addr3, addr4, 6);
    memcpy(addr4, addr5, 6);
    memcpy(addr5, addr6, 6);
    memcpy(addr6, addr7, 6);
    addr = addr + 8;
    readHex(addr + 48, line7);
    addrToHex(addr + 48, addr7);
}
void updateRow(int row) {
    if (row == 0) { readHex(addr, line1); }
    else if (row == 1) { readHex(addr + 8, line2); }
    else if (row == 2) { readHex(addr + 16, line3); }
    else if (row == 3) { readHex(addr + 24, line4); }
    else if (row == 4) { readHex(addr + 32, line5); }
    else if (row == 5) { readHex(addr + 40, line6); }
    else { readHex(addr + 48, line7); }
}
void moveLeft() {
    if (selection > 0) { selection = selection - 1; }
    else if (addr > -32768) { scrollUp(); selection = 7; }
}
void moveRight() {
    if (selection < 55) { selection = selection + 1; }
    else if (addr < 32712) { scrollDown(); selection = 48; }
}
void moveUp() {
    if (selection >= 8) { selection = selection - 8; }
    else if (addr > -32768) { scrollUp(); }
}
void moveDown() {
    if (selection < 48) { selection = selection + 8; }
    else if (addr < 32712) { scrollDown(); }
}
void advanceAfterWrite() {
    if (selection < 55) { selection = selection + 1; }
    else if (addr < 32712) { scrollDown(); selection = 48; }
}
void drawScreen() {
    print_ram(0, 0, addr1);
    print_ram(30, 0, line1);
    print_ram(0, 8, addr2);
    print_ram(30, 8, line2);
    print_ram(0, 16, addr3);
    print_ram(30, 16, line3);
    print_ram(0, 24, addr4);
    print_ram(30, 24, line4);
    print_ram(0, 32, addr5);
    print_ram(30, 32, line5);
    print_ram(0, 40, addr6);
    print_ram(30, 40, line6);
    print_ram(0, 48, addr7);
    print_ram(30, 48, line7);
    invert_rect(30 + (selection % 8) * 18, (selection / 8) * 8, 12, 8);
    drawStatus();
}
void drawStatus() {
    clear_rect(0, 56, 192, 8);
    print(0, 56, "pos:");
    addrToHex(addr + selection, status);
    print_ram(30, 56, status);
    if (nibble >= 0) {
        int_to_hex(nibble, nib, 1);
        print_ram(66, 56, nib);
    }
}
void drawRow(int row) {
    int y = row * 8;
    clear_rect(0, y, 192, 8);
    if (row == 0) { print_ram(0, y, addr1); print_ram(30, y, line1); }
    else if (row == 1) { print_ram(0, y, addr2); print_ram(30, y, line2); }
    else if (row == 2) { print_ram(0, y, addr3); print_ram(30, y, line3); }
    else if (row == 3) { print_ram(0, y, addr4); print_ram(30, y, line4); }
    else if (row == 4) { print_ram(0, y, addr5); print_ram(30, y, line5); }
    else if (row == 5) { print_ram(0, y, addr6); print_ram(30, y, line6); }
    else { print_ram(0, y, addr7); print_ram(30, y, line7); }
}
void optnMenu() {
    jagain = 1;
    while (jagain) {
        jagain = 0;
        clear();
        print(0, 0, "1. Jump to ...");
        print(0, 8, "2. Exit");
        print(0, 16, "AC: back");
        render();
        while (key() != -1) {}
        lastk = -1;
        k = -1;
        while (k != K_1 && k != K_2 && k != K_AC) {
            k = key();
            if (k == lastk) { k = -1; }
            else { lastk = k; }
            delay_ms(10);
        }
        if (k == K_AC) { return; }
        if (k == K_2) { exit(); }
        while (key() != -1) {}
        jumpto = -32768;
        jcount = 0;
        jumpbuf[0] = 0;
        clear();
        print(0, 0, "Where? (4 hex)");
        print(0, 8, "0x");
        render();
        while (jcount < 4) {
            k = key();
            if (k != lastk) {
                lastk = k;
                if (k != -1) {
                    if (k == K_AC) { return; }
                    else if (k == K_DEL) {
                        if (jcount > 0) {
                            jcount = jcount - 1;
                            jumpbuf[jcount] = 0;
                            jumpto = parseJumpAddress(jumpbuf, jcount);
                            clear_rect(18, 8, 60, 8);
                            print_ram(18, 8, jumpbuf);
                            render();
                        }
                    }
                    else {
                        v = keyToHex(k);
                        if (v >= 0) {
                            jumpbuf[jcount] = (char)(v < 10 ? 48 + v : 65 + v - 10);
                            jcount = jcount + 1;
                            jumpbuf[jcount] = 0;
                            jumpto = parseJumpAddress(jumpbuf, jcount);
                            clear_rect(18, 8, 60, 8);
                            print_ram(18, 8, jumpbuf);
                            render();
                        }
                    }
                }
            }
            delay_ms(10);
        }
        if (jumpto > 32712) {
            clear();
            print(0, 0, "Invalid address.");
            print(0, 16, "0000 - FFC8");
            render();
            delay_ms(500);
            jagain = 1;
        }
    }
    selection = jumpto & 7;
    addr = jumpto - selection;
    nibble = -1;
    updateAllLines(addr);
    lastk = -1;
}
void writeNibble(int value) {
    if (nibble < 0) { nibble = value; return; }
    poke(addrDecode(addr + selection), nibble * 16 + value);
    nibble = -1;
    wroteFlag = 1;
    writtenRow = selection / 8;
    updateRow(selection / 8);
    advanceAfterWrite();
}
void main() {
    clear();
    print(0, 0, "Hex Editor by flys_acc");
    print(0, 8, "press any key");
    render();
    while (key() == -1) {}
    while (key() != -1) {}
    clear();
    updateAllLines(addr);
    drawScreen();
    render();
    while (1) {
        k = key();
        if (k != lastk) {
            lastk = k;
            if (k != -1) {
                prevSel = selection;
                prevAddr = addr;
                prevNibble = nibble;
                wroteFlag = 0;
                fullFlag = 0;
                if (k == K_UP) { moveUp(); }
                else if (k == K_DOWN) { moveDown(); }
                else if (k == K_LEFT) { moveLeft(); }
                else if (k == K_RIGHT) { moveRight(); }
                else if (k == K_OPTN) { optnMenu(); fullFlag = 1; }
                else if (k == K_AC) { nibble = -1; }
                else {
                    v = keyToHex(k);
                    if (v >= 0) { writeNibble(v); }
                }
                if (fullFlag || addr != prevAddr) {
                    clear();
                    drawScreen();
                    render();
                }
                else if (wroteFlag) {
                    drawRow(writtenRow);
                    if (prevSel / 8 != writtenRow) { drawRow(prevSel / 8); }
                    if (selection / 8 != writtenRow && selection / 8 != prevSel / 8) {
                        drawRow(selection / 8);
                    }
                    invert_rect(30 + (selection % 8) * 18, (selection / 8) * 8, 12, 8);
                    drawStatus();
                    render();
                }
                else if (selection != prevSel || nibble != prevNibble) {
                    if (selection != prevSel) {
                        invert_rect(30 + (prevSel % 8) * 18, (prevSel / 8) * 8, 12, 8);
                        invert_rect(30 + (selection % 8) * 18, (selection / 8) * 8, 12, 8);
                    }
                    drawStatus();
                    render();
                }
            }
        }
        delay_ms(10);
    }
}