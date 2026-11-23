; NativeBoy - button test ROM (public domain), used by the Android test on GitHub.
; One box per button (Right, Left, Up, Down, A, B, Select, Start) that fills in while it is held,
; and a block (sprite) that moves with the d-pad.
; build: rgbasm -o bt.o botoes_test.asm && rgblink -o botoes_test.gb bt.o && rgbfix -v -p 0 -t BOTOES botoes_test.gb

DEF rP1   EQU $FF00
DEF rLCDC EQU $FF40
DEF rLY   EQU $FF44
DEF rBGP  EQU $FF47
DEF rOBP0 EQU $FF48
DEF rIE   EQU $FFFF
DEF SPR   EQU $FE00
DEF ROW   EQU $9800 + 8 * 32 + 2      ; the boxes: row 8, from column 2, one every 2 columns

SECTION "vblank", ROM0[$40]
    reti

SECTION "entry", ROM0[$100]
    nop
    jp Main
    ds $150 - @, 0

SECTION "main", ROM0[$150]
Main:
    di
    ld sp, $FFFE
.wait
    ldh a, [rLY]
    cp 144
    jr nz, .wait
    xor a
    ldh [rLCDC], a
    ld a, %11100100
    ldh [rBGP], a
    ldh [rOBP0], a
    ; tile 0 = empty, tile 1 = filled, tile 2 = outline
    ld hl, $8000
    ld b, 16
    xor a
.t0 ld [hl+], a
    dec b
    jr nz, .t0
    ld b, 16
    ld a, $FF
.t1 ld [hl+], a
    dec b
    jr nz, .t1
    ld de, Outline
    ld b, 16
.t2 ld a, [de]
    ld [hl+], a
    inc de
    dec b
    jr nz, .t2
    ; clear the map and the sprites
    ld hl, $9800
    ld bc, 32 * 32
.c  xor a
    ld [hl+], a
    dec bc
    ld a, b
    or c
    jr nz, .c
    ld hl, SPR
    ld b, 160
    xor a
.o  ld [hl+], a
    dec b
    jr nz, .o
    ; the block: y 100, x 84, tile 1
    ld hl, SPR
    ld a, 100
    ld [hl+], a
    ld a, 84
    ld [hl+], a
    ld a, 1
    ld [hl+], a
    xor a
    ld [hl], a
    ld a, %10010011          ; LCD on, tiles at $8000, sprites on, background on
    ldh [rLCDC], a
    ld a, 1
    ldh [rIE], a
    ei
Loop:
    halt
    call ReadPad             ; b = held buttons (1 = held)
    ; boxes
    ld hl, ROW
    ld c, 8
.box
    srl b
    ld a, 2
    jr nc, .up
    ld a, 1
.up ld [hl+], a
    inc hl
    dec c
    jr nz, .box
    ; move the block with the d-pad
    call ReadPad
    ld hl, SPR + 1
    bit 0, b
    jr z, .nr
    inc [hl]
.nr bit 1, b
    jr z, .nl
    dec [hl]
.nl ld hl, SPR
    bit 2, b
    jr z, .nu
    dec [hl]
.nu bit 3, b
    jr z, Loop
    inc [hl]
    jr Loop

; b = Right, Left, Up, Down, A, B, Select, Start in bits 0..7 (1 = held)
ReadPad:
    ld a, $20                ; d-pad
    ldh [rP1], a
    ldh a, [rP1]
    ldh a, [rP1]
    cpl
    and $0F
    ld b, a
    ld a, $10                ; buttons
    ldh [rP1], a
    ldh a, [rP1]
    ldh a, [rP1]
    ldh a, [rP1]
    cpl
    and $0F
    swap a
    or b
    ld b, a
    ld a, $30
    ldh [rP1], a
    ret

Outline:
    db $FF, $FF, $81, $81, $81, $81, $81, $81, $81, $81, $81, $81, $81, $81, $FF, $FF
