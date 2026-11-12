; NativeBoy - smart widescreen test ROM (public domain)
; A: whole map drawn (the sides are real) -> widescreen
; B: a "menu" redrawn only in the visible 20x18 area (the sides are leftovers) -> back to 160 columns
; C: the whole map is drawn again -> widescreen again
; build: rgbasm -o s.o smart_test.asm && rgblink -o smart_test.gb s.o && rgbfix -v -p 0 smart_test.gb
; (based on wide_test.asm)
; 1. fills the whole 32x32 background map with a dark tile ("previous screen")
; 2. waits ~3 s, then redraws only the 20 visible columns with a checker tile
; In widescreen, mode "mostrar tudo" shows the dark leftovers at the sides;
; mode "esconder trechos antigos" paints them with the background colour.
; build: rgbasm -o w.o wide_test.asm && rgblink -o wide_test.gb w.o && rgbfix -v -p 0 wide_test.gb

DEF rLCDC EQU $FF40
DEF rLY   EQU $FF44
DEF rBGP  EQU $FF47
DEF rIE   EQU $FFFF
DEF rIF   EQU $FF0F

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
    call LcdOff
    ld a, %11100100
    ldh [rBGP], a
    ; tiles: 0 = blank, 1 = solid darkest, 2 = checker
    ld hl, $8000
    ld bc, 16
    xor a
    call Fill
    ld bc, 16
    ld a, $FF
    call Fill
    ld b, 8
.chk
    ld a, $AA
    ld [hl+], a
    ld [hl+], a
    ld a, $55
    ld [hl+], a
    ld [hl+], a
    dec b
    jr nz, .chk
    ; "previous screen": whole map dark
    ld hl, $9800
    ld bc, 32 * 32
    ld a, 1
    call Fill
    ld a, $91
    ldh [rLCDC], a
    ld a, 1
    ldh [rIE], a
    ei
    ld b, 180
.wait
    halt
    dec b
    jr nz, .wait
    ; new screen: only the visible 20x18 area is redrawn
    call LcdOff
    ld hl, $9800
    ld d, 18
.row
    ld bc, 20
    ld a, 2
    call Fill
    ld bc, 12
    add hl, bc
    dec d
    jr nz, .row
    ld a, $91
    ldh [rLCDC], a
    ld b, 180
.wait2
    halt
    dec b
    jr nz, .wait2
    ; back to the "game": whole map drawn again
    call LcdOff
    ld hl, $9800
    ld bc, 32 * 32
    ld a, 1
    call Fill
    ld a, $91
    ldh [rLCDC], a
.idle
    halt
    jr .idle

LcdOff:
    ldh a, [rLY]
    cp 144
    jr nz, LcdOff
    xor a
    ldh [rLCDC], a
    ret

Fill:       ; hl = dest, bc = count, a = value
    ld e, a
.l
    ld a, e
    ld [hl+], a
    dec bc
    ld a, b
    or c
    jr nz, .l
    ret
