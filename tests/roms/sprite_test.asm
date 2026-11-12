; NativeBoy - sprites outside the screen test ROM (public domain)
; Background: tile 1 everywhere. Tile 2: solid colour 3 (8x8).
; Sprite 0 walks right 1 px per frame from OAM x=80 up to x=232 (screen x 224, outside the 256-wide picture).
; Sprite 1 is parked at OAM (0,0) (hidden the usual way).
; Sprite 2 sits on screen at (80,80) and at frame 60 jumps to OAM x=200 (hidden by teleporting it away).
; build: rgbasm -o s.o sprite_test.asm && rgblink -o sprite_test.gb s.o && rgbfix -v -p 0 sprite_test.gb
DEF rLCDC EQU $FF40
DEF rBGP  EQU $FF47
DEF rOBP0 EQU $FF48
DEF rLY   EQU $FF44
DEF rIE   EQU $FFFF

SECTION "vblank", ROM0[$40]
    reti

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
    ld a, %11100100
    ldh [rBGP], a
    ldh [rOBP0], a
    ; tile 1: checker pattern, tile 2: solid colour 3
    ld hl, $8010
    ld b, 8
.t1
    ld a, $AA
    ld [hl+], a
    ld a, $55
    ld [hl+], a
    dec b
    jr nz, .t1
    ld b, 16
.t2
    ld a, $FF
    ld [hl+], a
    dec b
    jr nz, .t2
    ; map: tile 1 everywhere
    ld hl, $9800
    ld bc, 1024
.map
    ld a, 1
    ld [hl+], a
    dec bc
    ld a, b
    or c
    jr nz, .map
    ; clear OAM
    ld hl, $FE00
    ld b, 160
    xor a
.oam
    ld [hl+], a
    dec b
    jr nz, .oam
    ; sprite 0: y=72 (screen 56), x=80
    ld hl, $FE00
    ld a, 72
    ld [hl+], a
    ld a, 80
    ld [hl+], a
    ld a, 2
    ld [hl+], a
    xor a
    ld [hl+], a
    ; sprite 1: parked at (0,0), tile 2
    xor a
    ld [hl+], a
    ld [hl+], a
    ld a, 2
    ld [hl+], a
    xor a
    ld [hl+], a
    ; sprite 2: y=96 (screen 80), x=88 (screen 80)
    ld a, 96
    ld [hl+], a
    ld a, 88
    ld [hl+], a
    ld a, 2
    ld [hl+], a
    xor a
    ld [hl+], a
    xor a
    ld [wFrame], a
    ld a, %10000011          ; LCD on, OBJ on, BG on, tiles at $8800/$8000 mode... use $8000:
    or %00010000
    ldh [rLCDC], a
    ld a, 1
    ldh [rIE], a
    ei
Loop:
    halt
    ld hl, wFrame
    inc [hl]
    ld a, [hl]
    cp 60
    jr nz, .walk
    ld a, 200                ; frame 60: sprite 2 teleports away (hidden)
    ld [$FE09], a
.walk
    ld a, [$FE01]
    cp 232
    jr z, Loop
    inc a
    ld [$FE01], a
    jr Loop

SECTION "vars", WRAM0[$C000]
wFrame: db
