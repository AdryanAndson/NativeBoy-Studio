; NativeBoy - MBC7 test ROM (public domain): accelerometer latch + 93LC56 EEPROM
; Serial output: "A" + "OK"/"NG" for the accelerometer centre (no tilt = $81D0),
; then "E" + "OK"/"NG" for EWEN, WRITE word 5 = $1234, READ word 5.
; build: rgbasm -o m7.o mbc7_test.asm && rgblink -o mbc7_test.gb m7.o && rgbfix -v -p 0 -m 0x22 mbc7_test.gb

MACRO EEBIT       ; clock one bit into the EEPROM (CS high)
    ld a, $80 | ((\1) << 1)
    ld [$A080], a
    ld a, $C0 | ((\1) << 1)
    ld [$A080], a
ENDM

SECTION "entry", ROM0[$100]
    nop
    jp Main
    ds $150 - @, 0

SECTION "main", ROM0[$150]
Main:
    di
    ld sp, $FFFE
    ld a, $0A
    ld [$0000], a
    ld a, $40
    ld [$4000], a
    ; ---- accelerometer
    ld a, "A"
    call Put
    ld a, $55
    ld [$A000], a
    ld a, $AA
    ld [$A010], a
    ld a, [$A020]
    cp $D0
    jr nz, .accel_bad
    ld a, [$A030]
    cp $81
    jr nz, .accel_bad
    call PutOK
    jr .eeprom
.accel_bad
    call PutNG
.eeprom
    ld a, "E"
    call Put
    xor a
    ld [$A080], a          ; CS low
    ; EWEN: 1 00 11xxxxxx
    EEBIT 1
    EEBIT 0
    EEBIT 0
    EEBIT 1
    EEBIT 1
    REPT 6
    EEBIT 0
    ENDR
    xor a
    ld [$A080], a
    ; WRITE word 5 = $1234: 1 01 00000101 + data
    EEBIT 1
    EEBIT 0
    EEBIT 1
    FOR BI, 7, -1, -1
    EEBIT (5 >> BI) & 1
    ENDR
    FOR BI, 15, -1, -1
    EEBIT ($1234 >> BI) & 1
    ENDR
    xor a
    ld [$A080], a
    ; READ word 5: 1 10 00000101, then 16 clocks
    EEBIT 1
    EEBIT 1
    EEBIT 0
    FOR BI, 7, -1, -1
    EEBIT (5 >> BI) & 1
    ENDR
    ld de, 0
    ld b, 16
.rd
    ld a, $80
    ld [$A080], a
    ld a, $C0
    ld [$A080], a
    ld a, [$A080]
    and 1
    sla e
    rl d
    or e
    ld e, a
    dec b
    jr nz, .rd
    xor a
    ld [$A080], a
    ld a, d
    cp $12
    jr nz, .ee_bad
    ld a, e
    cp $34
    jr nz, .ee_bad
    call PutOK
    jr .done
.ee_bad
    call PutNG
.done
    halt
    jr .done

PutOK:
    ld a, "O"
    call Put
    ld a, "K"
    jr Put
PutNG:
    ld a, "N"
    call Put
    ld a, "G"
Put:
    ldh [$FF01], a
    ld a, $81
    ldh [$FF02], a
    ret
