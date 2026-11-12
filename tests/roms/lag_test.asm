; NativeBoy - input lag test ROM (public domain)
; Like most games: VBlank applies what the game decided last frame (shadow register) and reads the
; buttons; the main loop then decides (A pressed: black screen, else white), shown on the next frame.
; Measures how many frames pass between pressing A and seeing it (run-ahead should remove them).
; build: rgbasm -o l.o lag_test.asm && rgblink -o lag_test.gb l.o && rgbfix -v -p 0 lag_test.gb
DEF rP1   EQU $FF00
DEF rLCDC EQU $FF40
DEF rBGP  EQU $FF47
DEF rLY   EQU $FF44
DEF rIE   EQU $FFFF

SECTION "vblank", ROM0[$40]
    jp VBlank

SECTION "header", ROM0[$100]
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
    ldh [rBGP], a
    ld hl, $8000              ; tile 0: blank (colour 0)
    ld b, 16
.t
    ld [hl+], a
    dec b
    jr nz, .t
    ld hl, $9800
    ld bc, 1024
.m
    xor a
    ld [hl+], a
    dec bc
    ld a, b
    or c
    jr nz, .m
    ld a, $FF
    ld [wJoy], a
    xor a
    ld [wShadow], a
    ld a, %10010001
    ldh [rLCDC], a
    ld a, 1
    ldh [rIE], a
    ei
Loop:
    halt
    ld a, [wJoy]
    bit 0, a                  ; 0 = A pressed
    ld a, $FF
    jr z, .set
    xor a
.set
    ld [wShadow], a
    jr Loop

VBlank:
    push af
    ld a, [wShadow]
    ldh [rBGP], a
    ld a, $10                 ; select the action buttons (A, B, Select, Start)
    ldh [rP1], a
    ldh a, [rP1]
    ldh a, [rP1]
    ldh a, [rP1]
    ld [wJoy], a
    ld a, $30
    ldh [rP1], a
    pop af
    reti

SECTION "vars", WRAM0[$C000]
wJoy:    db
wShadow: db
