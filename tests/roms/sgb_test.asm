; NativeBoy - Super Game Boy test ROM (public domain)
; 1. MLT_REQ detection -> serial 'S' (SGB found) or 'N'
; 2. CHR_TRN + PCT_TRN: blue frame border around the game screen
; 3. PAL01 + ATTR_BLK: left half uses palette 0 (white/red/green/blue),
;    right half palette 1 (white/yellow/magenta/cyan); screen shows 4 vertical
;    stripes of grey levels 0..3 (5 tiles wide each).
; build: rgbasm -o sgb.o sgb_test.asm && rgblink -o sgb_test.gb sgb.o && rgbfix -s -l 0x33 -v -p 0 sgb_test.gb

DEF rP1   EQU $FF00
DEF rSB   EQU $FF01
DEF rSC   EQU $FF02
DEF rLCDC EQU $FF40
DEF rLY   EQU $FF44
DEF rBGP  EQU $FF47

SECTION "entry", ROM0[$100]
    nop
    jp Main
    ds $150 - @, 0

SECTION "main", ROM0[$150]
Main:
    di
    ld sp, $FFFE
    ld a, 4
    call WaitFrames

    ; ---- 1. detection through multiplayer mode
    ld hl, PktMlt2
    call SendPacket
    ld a, $30
    ldh [rP1], a
    ldh a, [rP1]
    and $0F
    ld b, a
    ld a, $20
    ldh [rP1], a
    ldh a, [rP1]
    ld a, $10
    ldh [rP1], a
    ldh a, [rP1]
    ld a, $30
    ldh [rP1], a
    ldh a, [rP1]
    and $0F
    cp b
    ld a, "N"
    jr z, .report
    ld a, "S"
.report:
    ldh [rSB], a
    ld a, $81
    ldh [rSC], a
    ld hl, PktMlt1
    call SendPacket

    ; ---- 2a. border tiles (CHR_TRN): tile 0 transparent, tile 1 solid colour 1
    call LcdOff
    call ClearVram
    ld hl, $8020
    ld b, 8
.tile1:
    ld a, $FF
    ld [hl+], a
    xor a
    ld [hl+], a
    dec b
    jr nz, .tile1
    call TransferMap
    ld hl, PktChrTrn
    call SendPacket
    ld a, 3
    call WaitFrames

    ; ---- 2b. border map + palettes (PCT_TRN)
    call LcdOff
    call ClearVram
    ld hl, $8000
    ld c, 0                 ; row
.prow:
    ld b, 0                 ; column
.pcol:
    ; inside the game area (columns 6..25, rows 5..22) -> transparent tile 0
    ld a, b
    cp 6
    jr c, .frame
    cp 26
    jr nc, .frame
    ld a, c
    cp 5
    jr c, .frame
    cp 23
    jr nc, .frame
    xor a
    ld [hl+], a
    ld [hl+], a
    jr .pnext
.frame:
    ld a, $01               ; tile 1
    ld [hl+], a
    ld a, $10               ; palette 4
    ld [hl+], a
.pnext:
    inc b
    ld a, b
    cp 32
    jr nz, .pcol
    inc c
    ld a, c
    cp 32
    jr nz, .prow
    ; palette 4 at $8800: colour 1 = blue ($7C00)
    ld hl, $8802
    xor a
    ld [hl+], a
    ld a, $7C
    ld [hl], a
    call TransferMap
    ld hl, PktPctTrn
    call SendPacket
    ld a, 3
    call WaitFrames

    ; ---- 3. palettes + attribute block
    ld hl, PktPal01
    call SendPacket
    ld hl, PktAttrBlk
    call SendPacket

    ; stripes: tiles 0..3 = solid grey 0..3
    call LcdOff
    call ClearVram
    ld hl, $8000
    ld d, 0
.shade:
    ld e, 8
.srow:
    ld a, d
    and 1
    jr z, .lo0
    ld a, $FF
.lo0:
    ld [hl+], a
    ld a, d
    and 2
    jr z, .hi0
    ld a, $FF
.hi0:
    ld [hl+], a
    dec e
    jr nz, .srow
    inc d
    ld a, d
    cp 4
    jr nz, .shade
    ld hl, $9800
    ld c, 18
.mrow:
    ld b, 0
.mcol:
    ld a, b                 ; tile = column / 5
    ld e, 0
.div5:
    cp 5
    jr c, .divd
    sub 5
    inc e
    jr .div5
.divd:
    ld a, e
    ld [hl+], a
    inc b
    ld a, b
    cp 32
    jr nz, .mcol
    dec c
    jr nz, .mrow
    ld a, $E4
    ldh [rBGP], a
    ld a, $91
    ldh [rLCDC], a
.forever:
    halt
    jr .forever

; ---- helpers -------------------------------------------------------------
; send the 16-byte packet at HL
SendPacket:
    xor a
    ldh [rP1], a
    ld a, $30
    ldh [rP1], a
    ld b, 16
.byte:
    ld e, 8
    ld a, [hl+]
    ld d, a
.bit:
    bit 0, d
    ld a, $10
    jr nz, .one
    ld a, $20
.one:
    ldh [rP1], a
    ld a, $30
    ldh [rP1], a
    rr d
    dec e
    jr nz, .bit
    dec b
    jr nz, .byte
    ld a, $20
    ldh [rP1], a
    ld a, $30
    ldh [rP1], a
    ld a, 2
    jp WaitFrames

; wait A frames (polls LY)
WaitFrames:
    ld c, a
.f:
.w1:
    ldh a, [rLY]
    cp 144
    jr nz, .w1
.w2:
    ldh a, [rLY]
    cp 144
    jr z, .w2
    dec c
    jr nz, .f
    ret

LcdOff:
    ldh a, [rLCDC]
    bit 7, a
    ret z
.w:
    ldh a, [rLY]
    cp 144
    jr nz, .w
    xor a
    ldh [rLCDC], a
    ret

ClearVram:
    ld hl, $8000
    ld bc, $2000
.c:
    xor a
    ld [hl+], a
    dec bc
    ld a, b
    or c
    jr nz, .c
    ret

; BG map shows tiles 0..255 in order (20 per row) and the LCD is turned on
TransferMap:
    ld hl, $9800
    ld d, 0
    ld c, 13
.row:
    ld b, 20
.col:
    ld a, d
    ld [hl+], a
    inc d
    dec b
    jr nz, .col
    ld a, l
    add 12
    ld l, a
    jr nc, .nc
    inc h
.nc:
    dec c
    jr nz, .row
    ld a, $E4
    ldh [rBGP], a
    ld a, $91
    ldh [rLCDC], a
    ret

PktMlt2:    db ($11 << 3) | 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
PktMlt1:    db ($11 << 3) | 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
PktChrTrn:  db ($13 << 3) | 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
PktPctTrn:  db ($14 << 3) | 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
; PAL01: colour 0 white; pal0 = red, green, blue; pal1 = yellow, magenta, cyan
PktPal01:   db ($00 << 3) | 1
            dw $7FFF, $001F, $03E0, $7C00, $03FF, $7C1F, $7FE0
            db 0
; ATTR_BLK: one block x 10..19, y 0..17, inside+border palette 1, outside palette 0
PktAttrBlk: db ($04 << 3) | 1, 1, %00000111, %00000101, 10, 0, 19, 17, 0, 0, 0, 0, 0, 0, 0, 0
