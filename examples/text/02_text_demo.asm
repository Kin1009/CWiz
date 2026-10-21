_$$SP EQU 0F000h

dw _$$SP
dw $$start_up
dw _BRK
dw _INT
dw _INT
dw _INT
dw _INT
dw _INT
dw _INT
dw _INT
dw _INT
dw _INT
dw _INT
dw _INT
dw _INT
dw _INT
dw _INT
dw _INT
dw _INT
dw _INT
dw _INT
dw _INT

_BRK:
 mov psw, #3
 brk

_INT:
 rti

_060EC:
 PUSH LR
 MOV ER2, ER0
 TB 0F00AH.1
 BNE _$L074
_$L00A:
 DI
 MOV ER0, #01H
 ST ER0, 0F024H
 MOV ER0, #00H
 ST ER0, 0F022H
 ST ER2, 0F020H
 MOV R0, #01H
 ST R0, 0F025H
 MOV R0, #00H
 ST R0, 0F014H
 ST R0, 0F015H
 MOV R2, #50H
 MOV R3, #0A0H
 ST R2, 0F008H
 ST R3, 0F008H
 MOV R0, #02H
 ST R0, 0F009H
 NOP
 NOP
_$L044:
 L R0, 0F042H
 BNE _$L072
_$L04A:
 MOV ER0, #0FH
 ST ER0, 0F024H
 MOV ER0, #00H
 ST ER0, 0F022H
 MOV R0, #9EH
 MOV R1, #07H
 ST ER0, 0F020H
 MOV R0, #01H
 ST R0, 0F025H
 MOV R0, #00H
 ST R0, 0F014H
 ST R0, 0F015H
 EI
_$L072:
 POP PC
_$L074:
 RB 0F00AH.1
 BAL _$L00A

$$start_up:
 MOV R0, #00H
 ST R0, 0F000H
 B _2F060

_2F060:
 L R0, 0F058H
 MOV R4, R0
 BL _3DEDE
 MOV R0, #40H
 MOV R1, #06H
 BL _060EC
 MOV R0, #81H
 ST R0, 0F00AH
 MOV R0, #04H
 ST R0, 0F030H
 MOV R0, #03H
 ST R0, 0F033H
 MOV R0, #06H
 ST R0, 0F034H
 MOV R0, #17H
 ST R0, 0F035H
 MOV R0, #08H
 ST R0, 0F036H
 MOV R0, #00H
 ST R0, 0F039H
 MOV R0, #55H
 ST R0, 0F031H
 BL _entry


_3DEDE:
 PUSH LR
 MOV R0, #31H
 ST R0, 0F00AH
 MOV R0, #0F7H
 ST R0, 0F028H
 MOV R0, #32H
_$L010:
 ADD R0, #-1H
 CMP R0, #00H
 BNE _$L010
 MOV R0, #22H
 ST R0, 0F010H
 MOV R0, #00H
 ST R0, 0F011H
 ST R0, 0F012H
 MOV R0, #03H
 ST R0, 0F018H
 MOV R0, #00H
 ST R0, 0F058H
 MOV R0, #00H
 ST R0, 0F042H
 MOV R0, #07H
 ST R0, 0F03DH
 MOV R0, #0C8H
 MOV R1, #00H
_$L02E:
 BL _060EC
 MOV R0, #04H
 ST R0, 0F030H
 MOV R0, #07H
 ST R0, 0F033H
 MOV R0, #06H
 ST R0, 0F034H
 MOV R0, #17H
 ST R0, 0F035H
 MOV R0, #08H
 ST R0, 0F036H
 MOV R0, #00H
 ST R0, 0F039H
 MOV R0, #57H
 ST R0, 0F031H
 MOV R0, #12H
 ST R0, 0F032H
 MOV R0, #00H
 ST R0, 0F220H
 MOV R0, #7FH
 ST R0, 0F221H
 MOV R0, #00H
 ST R0, 0F222H
 MOV R0, #7FH
 ST R0, 0F223H
 MOV R0, #00H
 ST R0, 0F224H
 ST R0, 0F225H
 MOV R0, #00H
 ST R0, 0F048H
 ST R0, 0F049H
 MOV R0, #07H
 ST R0, 0F04AH
 MOV R0, #00H
 ST R0, 0F04BH
 MOV R0, #07H
 ST R0, 0F04CH
 MOV R0, #00H
 ST R0, 0F04EH
 MOV R0, #00H
 ST R0, 0F041H
 MOV R0, #80H
 ST R0, 0F044H
 MOV R0, #0FFH
 ST R0, 0F045H
 MOV R2, #00H
 ST R2, 0F046H
 POP PC

_entry:
    B __wiz_entry
 ;start your program here

__wiz_entry:
    BL __rt_init
    BL __init
    BL f_main
__wiz_halt:
    B __wiz_halt
__init:
    PUSH LR
    MOV ER0, #0
    POP PC

f_glyph:
    PUSH LR
    L ER0, 36868
    MOV ER2, #32
    CMP R0, R2
    CMPC R1, R3
    JLTS L3
    L ER0, 36868
    MOV R2, #126
    MOV R3, #0
    CMP R0, R2
    CMPC R1, R3
    JLES L1
L3:
    MOV ER0, #63
    ST ER0, 36868
    B L2
L1:
L2:
    L ER0, 36868
    MOV ER2, #32
    SUB R0, R2
    SUBC R1, R3
    MOV ER2, #8
    BL __mul16
    ST ER0, 36870
    MOV ER0, #0
    ST ER0, 36872
L4:
    L ER0, 36872
    MOV ER2, #8
    CMP R0, R2
    CMPC R1, R3
    JGES L5
    L ER0, 36870
    L ER2, 36872
    ADD ER0, ER2
    MOV R2, #(__d__font & 255)
    MOV R3, #((__d__font >> 8) & 255)
    ADD ER0, ER2
    MOV ER2, ER0
    L R0, [ER2]
    MOV R1, #0
    ST ER0, 36874
    MOV ER0, #0
    ST ER0, 36876
L6:
    L ER0, 36876
    MOV ER2, #6
    CMP R0, R2
    CMPC R1, R3
    JGES L7
    L ER0, 36864
    L ER2, 36876
    ADD ER0, ER2
    PUSH ER0
    L ER0, 36866
    L ER2, 36872
    ADD ER0, ER2
    PUSH ER0
    L ER0, 36874
    MOV R2, #128
    MOV R3, #0
    AND R0, R2
    AND R1, R3
    MOV ER2, #0
    CMP R0, R2
    CMPC R1, R3
    JEQ L8
    MOV ER0, #1
    B L9
L8:
    MOV ER0, #0
L9:
    PUSH ER0
    POP ER4
    POP ER2
    POP ER0
    BL __pixel
    L ER0, 36874
    L ER2, 36874
    ADD ER0, ER2
    ST ER0, 36874
    L ER0, 36876
    MOV ER2, #1
    ADD ER0, ER2
    ST ER0, 36876
    B L6
L7:
    L ER0, 36872
    MOV ER2, #1
    ADD ER0, ER2
    ST ER0, 36872
    B L4
L5:
    MOV ER0, #0
f_glyph__ret:
    POP PC

f_text:
    PUSH LR
    L ER0, 36882
    PUSH ER0
    POP ER0
    BL __peek
    ST ER0, 36884
L10:
    L ER0, 36884
    MOV ER2, #0
    CMP R0, R2
    CMPC R1, R3
    JEQ L11
    L ER0, 36878
    PUSH ER0
    L ER0, 36880
    PUSH ER0
    L ER0, 36884
    PUSH ER0
    POP ER0
    ST ER0, 36868
    POP ER0
    ST ER0, 36866
    POP ER0
    ST ER0, 36864
    BL f_glyph
    L ER0, 36878
    MOV ER2, #6
    ADD ER0, ER2
    ST ER0, 36878
    L ER0, 36882
    MOV ER2, #1
    ADD ER0, ER2
    ST ER0, 36882
    L ER0, 36882
    PUSH ER0
    POP ER0
    BL __peek
    ST ER0, 36884
    B L10
L11:
    L ER0, 36878
    B f_text__ret
    MOV ER0, #0
f_text__ret:
    POP PC

f_num:
    PUSH LR
    L ER0, 36890
    PUSH ER0
    MOV R0, #0
    MOV R1, #128
    MOV ER2, ER0
    MOV ER0, #0
    SUB R0, R2
    SUBC R1, R3
    MOV ER2, ER0
    POP ER0
    CMP R0, R2
    CMPC R1, R3
    JNE L12
    L ER0, 36886
    PUSH ER0
    L ER0, 36888
    PUSH ER0
    MOV R0, #(__s0 & 255)
    MOV R1, #((__s0 >> 8) & 255)
    PUSH ER0
    POP ER0
    ST ER0, 36882
    POP ER0
    ST ER0, 36880
    POP ER0
    ST ER0, 36878
    BL f_text
    B f_num__ret
    B L13
L12:
L13:
    L ER0, 36890
    MOV ER2, #0
    CMP R0, R2
    CMPC R1, R3
    JGES L14
    L ER0, 36886
    PUSH ER0
    L ER0, 36888
    PUSH ER0
    MOV ER0, #45
    PUSH ER0
    POP ER0
    ST ER0, 36868
    POP ER0
    ST ER0, 36866
    POP ER0
    ST ER0, 36864
    BL f_glyph
    L ER0, 36886
    MOV ER2, #6
    ADD ER0, ER2
    ST ER0, 36886
    MOV ER0, #0
    L ER2, 36890
    SUB R0, R2
    SUBC R1, R3
    ST ER0, 36890
    B L15
L14:
L15:
    MOV R0, #16
    MOV R1, #39
    ST ER0, 36892
    MOV ER0, #0
    ST ER0, 36894
L16:
    L ER0, 36892
    MOV ER2, #0
    CMP R0, R2
    CMPC R1, R3
    JLES L17
    L ER0, 36890
    L ER2, 36892
    BL __div16
    ST ER0, 36896
    L ER0, 36890
    PUSH ER0
    L ER0, 36896
    L ER2, 36892
    BL __mul16
    MOV ER2, ER0
    POP ER0
    SUB R0, R2
    SUBC R1, R3
    ST ER0, 36890
    L ER0, 36896
    MOV ER2, #0
    CMP R0, R2
    CMPC R1, R3
    JGTS L20
    L ER0, 36894
    OR R0, R1
    JNE L20
    L ER0, 36892
    MOV ER2, #1
    CMP R0, R2
    CMPC R1, R3
    JNE L18
L20:
    L ER0, 36886
    PUSH ER0
    L ER0, 36888
    PUSH ER0
    MOV ER0, #48
    L ER2, 36896
    ADD ER0, ER2
    PUSH ER0
    POP ER0
    ST ER0, 36868
    POP ER0
    ST ER0, 36866
    POP ER0
    ST ER0, 36864
    BL f_glyph
    L ER0, 36886
    MOV ER2, #6
    ADD ER0, ER2
    ST ER0, 36886
    MOV ER0, #1
    ST ER0, 36894
    B L19
L18:
L19:
    L ER0, 36892
    MOV ER2, #10
    BL __div16
    ST ER0, 36892
    B L16
L17:
    L ER0, 36886
    B f_num__ret
    MOV ER0, #0
f_num__ret:
    POP PC

f_wait_key:
    PUSH LR
L21:
    BL __key
    PUSH ER0
    MOV ER0, #1
    MOV ER2, ER0
    MOV ER0, #0
    SUB R0, R2
    SUBC R1, R3
    MOV ER2, ER0
    POP ER0
    CMP R0, R2
    CMPC R1, R3
    JEQ L22
    B L21
L22:
L23:
    BL __key
    ST ER0, 36898
    L ER0, 36898
    PUSH ER0
    MOV ER0, #1
    MOV ER2, ER0
    MOV ER0, #0
    SUB R0, R2
    SUBC R1, R3
    MOV ER2, ER0
    POP ER0
    CMP R0, R2
    CMPC R1, R3
    JEQ L25
    L ER0, 36898
    B f_wait_key__ret
    B L26
L25:
L26:
    B L23
L24:
    MOV ER0, #0
f_wait_key__ret:
    POP PC

f_main:
    PUSH LR
    BL __clear
    MOV ER0, #4
    ST ER0, 36900
    MOV ER0, #8
    ST ER0, 36902
    L ER0, 36900
    PUSH ER0
    L ER0, 36902
    PUSH ER0
    MOV R0, #(__s1 & 255)
    MOV R1, #((__s1 >> 8) & 255)
    PUSH ER0
    POP ER0
    ST ER0, 36882
    POP ER0
    ST ER0, 36880
    POP ER0
    ST ER0, 36878
    BL f_text
    ST ER0, 36900
    MOV ER0, #4
    ST ER0, 36900
    MOV ER0, #20
    ST ER0, 36902
    L ER0, 36900
    PUSH ER0
    L ER0, 36902
    PUSH ER0
    MOV R0, #(__s2 & 255)
    MOV R1, #((__s2 >> 8) & 255)
    PUSH ER0
    POP ER0
    ST ER0, 36882
    POP ER0
    ST ER0, 36880
    POP ER0
    ST ER0, 36878
    BL f_text
    ST ER0, 36900
    MOV ER0, #4
    ST ER0, 36900
    MOV ER0, #32
    ST ER0, 36902
    L ER0, 36900
    PUSH ER0
    L ER0, 36902
    PUSH ER0
    MOV R0, #(__s3 & 255)
    MOV R1, #((__s3 >> 8) & 255)
    PUSH ER0
    POP ER0
    ST ER0, 36882
    POP ER0
    ST ER0, 36880
    POP ER0
    ST ER0, 36878
    BL f_text
    ST ER0, 36900
    BL f_wait_key
    MOV ER0, #0
f_main__ret:
    POP PC

    ; --- strings ---
__s0:
    DB 45, 51, 50, 55, 54, 56, 0
    DB 0
__s1:
    DB 84, 69, 88, 84, 32, 68, 69, 77, 79, 0
__s2:
    DB 83, 99, 111, 114, 101, 58, 32, 57, 57, 0
__s3:
    DB 72, 101, 108, 108, 111, 44, 32, 87, 105, 122, 33, 0

    ; --- table _font ---
__d__font:
    DB 0, 0, 0, 0, 0, 0, 0, 0, 32, 32, 32, 32
    DB 32, 0, 32, 0, 80, 80, 0, 0, 0, 0, 0, 0
    DB 80, 80, 248, 80, 248, 80, 80, 0, 32, 120, 160, 112
    DB 40, 240, 32, 0, 0, 200, 208, 32, 32, 88, 152, 0
    DB 112, 80, 32, 104, 144, 144, 104, 0, 32, 32, 0, 0
    DB 0, 0, 0, 0, 16, 32, 64, 64, 64, 32, 16, 0
    DB 32, 16, 8, 8, 8, 16, 32, 0, 32, 168, 112, 248
    DB 112, 168, 32, 0, 0, 32, 32, 248, 32, 32, 0, 0
    DB 0, 0, 0, 0, 0, 16, 32, 0, 0, 0, 0, 248
    DB 0, 0, 0, 0, 0, 0, 0, 0, 0, 16, 0, 0
    DB 8, 16, 16, 32, 64, 64, 128, 0, 112, 136, 152, 168
    DB 200, 136, 112, 0, 48, 16, 16, 16, 16, 16, 16, 0
    DB 112, 136, 8, 16, 96, 128, 248, 0, 112, 136, 8, 48
    DB 8, 136, 112, 0, 48, 80, 80, 80, 144, 248, 16, 0
    DB 248, 128, 128, 240, 8, 8, 240, 0, 112, 136, 128, 240
    DB 136, 136, 112, 0, 248, 8, 16, 32, 32, 64, 64, 0
    DB 112, 136, 136, 112, 136, 136, 112, 0, 112, 136, 136, 120
    DB 8, 136, 112, 0, 0, 0, 16, 0, 0, 16, 0, 0
    DB 0, 0, 16, 0, 0, 16, 32, 0, 0, 16, 32, 64
    DB 32, 16, 0, 0, 0, 0, 120, 0, 120, 0, 0, 0
    DB 0, 64, 32, 16, 32, 64, 0, 0, 112, 136, 136, 48
    DB 32, 0, 32, 0, 112, 136, 184, 168, 184, 128, 120, 0
    DB 112, 136, 136, 136, 248, 136, 136, 0, 240, 136, 136, 240
    DB 136, 136, 240, 0, 112, 136, 128, 128, 128, 136, 112, 0
    DB 224, 144, 136, 136, 136, 144, 224, 0, 248, 128, 128, 240
    DB 128, 128, 248, 0, 248, 128, 128, 240, 128, 128, 128, 0
    DB 112, 136, 128, 128, 184, 136, 120, 0, 136, 136, 136, 248
    DB 136, 136, 136, 0, 112, 32, 32, 32, 32, 32, 112, 0
    DB 56, 8, 8, 8, 136, 136, 112, 0, 136, 144, 160, 192
    DB 160, 144, 136, 0, 128, 128, 128, 128, 128, 128, 248, 0
    DB 136, 216, 216, 168, 168, 136, 136, 0, 136, 200, 168, 168
    DB 168, 152, 136, 0, 112, 136, 136, 136, 136, 136, 112, 0
    DB 240, 136, 136, 240, 128, 128, 128, 0, 112, 136, 136, 136
    DB 168, 152, 120, 0, 240, 136, 136, 240, 144, 136, 136, 0
    DB 112, 136, 128, 112, 8, 136, 112, 0, 248, 32, 32, 32
    DB 32, 32, 32, 0, 136, 136, 136, 136, 136, 136, 112, 0
    DB 136, 136, 136, 80, 80, 32, 32, 0, 136, 136, 168, 168
    DB 216, 216, 136, 0, 136, 80, 80, 32, 80, 80, 136, 0
    DB 136, 136, 80, 80, 32, 32, 32, 0, 248, 8, 16, 32
    DB 64, 128, 248, 0, 48, 32, 32, 32, 32, 32, 48, 0
    DB 128, 64, 64, 32, 16, 16, 8, 0, 48, 16, 16, 16
    DB 16, 16, 48, 0, 32, 80, 0, 0, 0, 0, 0, 0
    DB 0, 0, 0, 0, 0, 0, 248, 0, 64, 32, 16, 0
    DB 0, 0, 0, 0, 0, 0, 240, 8, 120, 136, 116, 0
    DB 128, 128, 240, 136, 136, 136, 240, 0, 0, 0, 120, 128
    DB 128, 128, 120, 0, 8, 8, 120, 136, 136, 136, 120, 0
    DB 0, 0, 120, 136, 248, 128, 120, 0, 48, 32, 32, 112
    DB 32, 32, 32, 0, 0, 0, 120, 136, 248, 8, 240, 0
    DB 128, 128, 240, 136, 136, 136, 136, 0, 0, 32, 0, 32
    DB 32, 32, 32, 0, 0, 8, 0, 8, 8, 136, 112, 0
    DB 128, 128, 128, 152, 224, 144, 136, 0, 48, 16, 16, 16
    DB 16, 16, 56, 0, 0, 0, 208, 168, 168, 168, 168, 0
    DB 0, 0, 240, 136, 136, 136, 136, 0, 0, 0, 112, 136
    DB 136, 136, 112, 0, 0, 0, 240, 136, 136, 240, 128, 0
    DB 0, 0, 120, 136, 136, 120, 8, 0, 0, 0, 184, 192
    DB 128, 128, 128, 0, 0, 0, 120, 128, 248, 8, 240, 0
    DB 0, 32, 112, 32, 32, 32, 48, 0, 0, 0, 136, 136
    DB 136, 136, 120, 0, 0, 0, 136, 136, 80, 80, 32, 0
    DB 0, 0, 136, 168, 168, 216, 136, 0, 0, 0, 136, 80
    DB 32, 80, 136, 0, 0, 0, 136, 80, 32, 64, 128, 0
    DB 0, 0, 248, 16, 32, 64, 248, 0, 16, 32, 32, 96
    DB 32, 32, 16, 0, 32, 32, 32, 32, 32, 32, 32, 0
    DB 32, 16, 16, 24, 16, 16, 32, 0, 88, 160, 0, 0
    DB 0, 0, 0, 0
_ROWS EQU 64
; Build tags: a line ending in ";@2p" is kept only for 2-bitplane targets (cwii), ";@1p" only for
; 1-bitplane targets (cw). The plane loops step R12 = 0,4 (and select the plane through 0F037H) on cwii;
; on cw the same loop runs once and never touches 0F037H.
; ---- Wiz runtime. Convention: args ER0,ER2,ER4 ; result ER0 (ER2 = remainder for div).
; Routines may clobber ER2-ER14. Leaf routines return with RT; the few that BL to another
; routine (__buffered, __key_hit) do PUSH LR ... POP PC like compiled functions.
;
; Runtime RAM (above the variable area 0x9000-0xDFFF, below the stack that grows down from 0xF000):
;   0xE000-0xE7FF  back buffer (same layout as LCD VRAM: y*32 + x/8, one plane, black/white)
;   0xE800         draw mode: 0 = draw straight to the LCD, 1 = draw to back buffer (see buffered/flip)
;   0xE808-0xE80F  key_hit edge-detect bitmap (1 bit per scan index)

; ER0 * ER2 -> ER0 (low 16 bits)
__mul16:
    MOV ER4, ER0
    MOV ER0, #0
    MOV R6, #16
__mul16_l:
    TB R2.0
    BEQ __mul16_s
    ADD ER0, ER4
__mul16_s:
    ADD ER4, ER4
    SRLC R2, #1
    SRL R3, #1
    ADD R6, #-1
    BNE __mul16_l
    RT

; signed ER0 / ER2 -> ER0 quotient (trunc to zero), ER2 remainder (sign of dividend)
__div16:
    MOV R6, R2
    OR R6, R3
    BNE __div16_ok
    MOV ER0, #0
    MOV ER2, #0
    RT
__div16_ok:
    MOV R7, #0
    TB R1.7
    BEQ __div16_a
    MOV ER4, #0
    SUB R4, R0
    SUBC R5, R1
    MOV ER0, ER4
    MOV R7, #3
__div16_a:
    TB R3.7
    BEQ __div16_b
    MOV ER4, #0
    SUB R4, R2
    SUBC R5, R3
    MOV ER2, ER4
    XOR R7, #1
__div16_b:
    MOV ER4, #0
    MOV R6, #16
__div16_l:
    ADD ER0, ER0
    ADDC R4, R4
    ADDC R5, R5
    CMP R4, R2
    CMPC R5, R3
    BLT __div16_n
    SUB R4, R2
    SUBC R5, R3
    OR R0, #1
__div16_n:
    ADD R6, #-1
    BNE __div16_l
    MOV ER2, ER4
    TB R7.0
    BEQ __div16_q
    MOV ER4, #0
    SUB R4, R0
    SUBC R5, R1
    MOV ER0, ER4
__div16_q:
    TB R7.1
    BEQ __div16_r
    MOV ER4, #0
    SUB R4, R2
    SUBC R5, R3
    MOV ER2, ER4
__div16_r:
    RT

; ER0 << R2
__shl16:
    CMP R2, #0
    BEQ __shl16_e
__shl16_l:
    ADD ER0, ER0
    ADD R2, #-1
    BNE __shl16_l
__shl16_e:
    RT

; ER0 >> R2 (arithmetic)
__sra16:
    CMP R2, #0
    BEQ __sra16_e
__sra16_l:
    SRLC R0, #1
    SRA R1, #1
    ADD R2, #-1
    BNE __sra16_l
__sra16_e:
    RT

; pixel(x=ER0, y=ER2, c=R4) : c=0 clears, nonzero sets. Off-screen coordinates are ignored.
; mode 0: both LCD bitplanes (black).  mode 1: the back buffer at 0xE000.
__pixel:
    CMP R1, #0
    BNE __pixel_x
    CMP R0, #0C0H
    BGE __pixel_x
    CMP R3, #0
    BNE __pixel_x
    CMP R2, #_ROWS
    BGE __pixel_x
    MOV R9, R4
    MOV R10, #32
    MOV R6, R2
    MUL ER6, R10
    MOV R10, R0
    SRL R10, #3
    MOV R11, #0
    ADD ER6, ER10
    MOV R8, #80H
    MOV R10, R0
    AND R10, #7
    SRL R8, R10
    MOV R11, R8
    XOR R11, #0FFH
    L R10, 0E800H
    CMP R10, #0
    BNE __pixel_b
    ADD R7, #0F8H
    MOV R12, #0
__pixel_p:
    ST R12, 0F037H ;@2p
    L R10, [ER6]
    CMP R9, #0
    BEQ __pixel_c
    OR R10, R8
    BAL __pixel_w
__pixel_c:
    AND R10, R11
__pixel_w:
    ST R10, [ER6]
    ADD R12, #4
    CMP R12, #8 ;@2p
    BNE __pixel_p
    RT
__pixel_b:
    ADD R7, #0E0H
    L R10, [ER6]
    CMP R9, #0
    BEQ __pixel_bc
    OR R10, R8
    BAL __pixel_bw
__pixel_bc:
    AND R10, R11
__pixel_bw:
    ST R10, [ER6]
__pixel_x:
    RT

; clear the LCD (mode 0, both bitplanes) or the back buffer (mode 1)
__clear:
    L R10, 0E800H
    CMP R10, #0
    BNE __clear_b
    MOV R12, #0
__clear_p:
    ST R12, 0F037H ;@2p
    MOV R6, #0
    MOV R7, #0F8H
    MOV R8, #0
    MOV R9, #4
    MOV ER10, #0
__clear_l:
    ST ER10, [ER6]
    ADD ER6, #2
    ADD ER8, #-1
    BNE __clear_l
    ADD R12, #4
    CMP R12, #8 ;@2p
    BNE __clear_p
    RT
__clear_b:
    MOV R6, #0
    MOV R7, #0E0H
    MOV R8, #0
    MOV R9, #4
    MOV ER10, #0
__clear_bl:
    ST ER10, [ER6]
    ADD ER6, #2
    ADD ER8, #-1
    BNE __clear_bl
    RT

; buffered(on): on != 0 -> draw into the back buffer (cleared now); on == 0 -> draw straight to the LCD
__buffered:
    ST R0, 0E800H
    CMP R0, #0
    BEQ __buffered_e
    PUSH LR
    BL __clear
    POP PC
__buffered_e:
    RT

; flip(): copy the back buffer to both LCD bitplanes (shows the finished frame)
__flip:
    MOV R12, #0
__flip_p:
    ST R12, 0F037H ;@2p
    MOV R6, #0
    MOV R7, #0E0H
    MOV R10, #0
    MOV R11, #0F8H
    MOV R8, #0
    MOV R9, #4
__flip_l:
    L ER4, [ER6]
    ST ER4, [ER10]
    ADD ER6, #2
    ADD ER10, #2
    ADD ER8, #-1
    BNE __flip_l
    ADD R12, #4
    CMP R12, #8 ;@2p
    BNE __flip_p
    RT

; runtime state init (called once at entry, before top-level code)
__rt_init:
    MOV R0, #0
    ST R0, 0E800H
    ST R0, 0E808H
    ST R0, 0E809H
    ST R0, 0E80AH
    ST R0, 0E80BH
    ST R0, 0E80CH
    ST R0, 0E80DH
    ST R0, 0E80EH
    ST R0, 0E80FH
    RT

; key_down(k=ER0) -> ER0 = 1 while scan index k is held, else 0
__key_down:
    AND R0, #3FH
    MOV R2, R0
    SRL R2, #3
    MOV R3, #80H
    SRL R3, R2
    AND R0, #7
    MOV R4, #80H
    SRL R4, R0
    ST R3, 0F046H
    L R5, 0F040H
    AND R5, R4
    BNE __key_down_n
    MOV ER0, #1
    RT
__key_down_n:
    MOV ER0, #0
    RT

; key_hit(k=ER0) -> ER0 = 1 only on the call where k went from released to held
; (call it every loop iteration for every key you care about)
__key_hit:
    PUSH LR
    PUSH ER0
    BL __key_down
    POP ER2
    AND R2, #3FH
    MOV R4, R2
    SRL R4, #3
    MOV R5, #0
    MOV R6, #8
    MOV R7, #0E8H
    ADD ER6, ER4
    AND R2, #7
    MOV R8, #80H
    SRL R8, R2
    L R9, [ER6]
    CMP R0, #0
    BEQ __key_hit_up
    MOV R10, R9
    AND R10, R8
    BNE __key_hit_no
    OR R9, R8
    ST R9, [ER6]
    MOV ER0, #1
    POP PC
__key_hit_up:
    MOV R10, R8
    XOR R10, #0FFH
    AND R9, R10
    ST R9, [ER6]
__key_hit_no:
    MOV ER0, #0
    POP PC

; key() -> ER0 = scan index 0..63 (8*KO bit + KI bit, MSB first) or -1
__key:
    MOV R0, #0
    MOV R1, #0
    MOV R2, #80H
__key_x:
    ST R2, 0F046H
    MOV R3, #80H
__key_y:
    L R4, 0F040H
    AND R4, R3
    BEQ __key_f
    ADD R0, #1
    SRL R3, #1
    CMP R3, #0
    BNE __key_y
    SRL R2, #1
    CMP R2, #0
    BNE __key_x
    MOV ER0, #-1
__key_f:
    RT

; delay(n): n * ~ (busy loop) -- UNCALIBRATED, units arbitrary until measured on hardware
__delay:
    MOV ER4, ER0
__delay_o:
    MOV R6, #0
    MOV R7, #1
__delay_i:
    ADD ER6, #-1
    BNE __delay_i
    ADD ER4, #-1
    BNE __delay_o
    RT

; peek(addr=ER0) -> ER0 = unsigned byte at addr (RAM, SFR or ROM)
__peek:
    MOV ER2, ER0
    L R0, [ER2]
    MOV R1, #0
    RT

; _rpeek(addr=ER0) -> ER0: like peek, but in data segment _ROMSEG (ROM tables/strings of a program that lives in segment 3)

; poke(addr=ER0, v=ER2): store the low byte of v at addr
__poke:
    ST R2, [ER0]
    RT

; _hspan(x0=ER0, x1=ER2, y=ER4, c=R6): fill pixels x0..x1-1 on row y. Caller guarantees 0<=x0<x1<=192, 0<=y<_ROWS.
; c=0 clears, else sets. Works on the LCD (both bitplanes) or the back buffer, like pixel().
__hspan:
    MOV R8, R4
    MOV R9, #0
    MOV R10, #32
    MUL ER8, R10
    MOV R11, R0
    SRL R11, #3
    MOV R10, R0
    AND R10, #7
    MOV R7, #0FFH
    SRL R7, R10
    MOV R12, R2
    ADD R12, #-1
    MOV R13, R12
    SRL R13, #3
    AND R12, #7
    MOV R3, #7
    SUB R3, R12
    MOV R5, #0FFH
    SLL R5, R3
    SUB R13, R11
    MOV R10, R11
    MOV R11, #0
    ADD ER8, ER10
    L R3, 0E800H
    CMP R3, #0
    BNE __hs_b
    ADD R9, #0F8H
    BAL __hs_go
__hs_b:
    ADD R9, #0E0H
__hs_go:
    MOV R12, #0
__hs_plane:
    L R3, 0E800H
    CMP R3, #0
    BNE __hs_ns
    ST R12, 0F037H ;@2p
__hs_ns:
    MOV ER10, ER8
    CMP R13, #0
    BNE __hs_multi
    MOV R3, R7
    AND R3, R5
    L R4, [ER10]
    CMP R6, #0
    BEQ __hs_s0
    OR R4, R3
    BAL __hs_s1
__hs_s0:
    XOR R3, #0FFH
    AND R4, R3
__hs_s1:
    ST R4, [ER10]
    BAL __hs_next
__hs_multi:
    L R4, [ER10]
    MOV R3, R7
    CMP R6, #0
    BEQ __hs_f0
    OR R4, R3
    BAL __hs_f1
__hs_f0:
    XOR R3, #0FFH
    AND R4, R3
__hs_f1:
    ST R4, [ER10]
    ADD ER10, #1
    MOV R3, R13
    ADD R3, #-1
    BEQ __hs_last
__hs_mid:
    MOV R4, #0
    CMP R6, #0
    BEQ __hs_m0
    MOV R4, #0FFH
__hs_m0:
    ST R4, [ER10]
    ADD ER10, #1
    ADD R3, #-1
    BNE __hs_mid
__hs_last:
    L R4, [ER10]
    MOV R3, R5
    CMP R6, #0
    BEQ __hs_l0
    OR R4, R3
    BAL __hs_l1
__hs_l0:
    XOR R3, #0FFH
    AND R4, R3
__hs_l1:
    ST R4, [ER10]
__hs_next:
    ADD R12, #4
    L R3, 0E800H
    CMP R3, #0
    BNE __hs_done
    CMP R12, #8 ;@2p
    BNE __hs_plane
__hs_done:
    RT
