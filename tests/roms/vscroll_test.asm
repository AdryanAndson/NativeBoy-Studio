; NativeBoy - taller picture (vertical expansion) test ROM (public domain)
; Scrolls right 1 pixel per frame. The map column that leaves the screen is cleared at once. Like many games, it keeps re-drawing only the columns that
; are on screen (3 per frame, round robin), so map columns that went off-screen become stale.
; World column c uses tile (c mod 15) + 1. A window HUD bar covers the last 8 lines.
; build: rgbasm -o sc.o scroll_test.asm && rgblink -o scroll_test.gb sc.o && rgbfix -v -p 0 scroll_test.gb

DEF rLCDC EQU $FF40
DEF rSCX  EQU $FF43
DEF rSCY  EQU $FF42
DEF rWY   EQU $FF4A
DEF rWX   EQU $FF4B
DEF rBGP  EQU $FF47
DEF rLY   EQU $FF44
DEF rIE   EQU $FFFF

SECTION "vblank", ROM0[$40]
    reti

SECTION "entry", ROM0[$100]
    nop
    jp Main
    ds $150 - @, 0

SECTION "main", ROM0[$150]
; Scrolls down 1 pixel per frame. World row r uses tile (r mod 15) + 1. Like many games it keeps
; re-drawing only the rows on screen (3 per frame) and clears the row that just left at the top.
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
    ; tiles 1..15: each one a different pattern that also changes from line to line
    ld hl, $8010
    ld b, 1
.tiles
    ld a, b
    ld c, a
    add a
    add a
    add a
    add c
    ld d, a
    ld a, c
    swap a
    xor c
    ld e, a
    ld c, 8
.trow
    ld a, d
    ld [hl+], a
    ld a, e
    ld [hl+], a
    rlc d
    rrc e
    dec c
    jr nz, .trow
    inc b
    ld a, b
    cp 16
    jr nz, .tiles
    xor a
    ld [wCol], a
.init
    ld a, [wCol]
    call DrawRow
    ld hl, wCol
    inc [hl]
    ld a, [hl]
    cp 32
    jr nz, .init
    xor a
    ld [wRR], a
    ld [wBase], a
    ldh [rSCX], a
    ldh [rSCY], a
    ld a, %10010001          ; LCD on, tiles at $8000, BG on
    ldh [rLCDC], a
    ld a, 1
    ldh [rIE], a
    ei
Loop:
    halt
    ldh a, [rSCY]
    inc a
    ldh [rSCY], a
    ld b, 3
.redraw
    push bc
    ld a, [wRR]
    inc a
    cp 19
    jr c, .ok
    xor a
.ok
    ld [wRR], a
    ld b, a
    ldh a, [rSCY]
    srl a
    srl a
    srl a
    add b
    ld c, a
    ld a, [wBase]
    add c
    call DrawRowAt
    pop bc
    dec b
    jr nz, .redraw
    ldh a, [rSCY]
    srl a
    srl a
    srl a
    dec a
    ld c, a
    xor a
    ld [wTile], a
    call ClearRow
    ldh a, [rSCY]
    and a
    jr nz, Loop
    ld a, [wBase]
    add 32
    ld [wBase], a
    jr Loop

DrawRow:
    ld c, a
DrawRowAt:               ; a = world row, c = map row
.mod
    cp 15
    jr c, .done
    sub 15
    jr .mod
.done
    inc a
    ld [wTile], a
ClearRow:                ; writes [wTile] into map row c (all 32 columns)
    ld a, c
    and 31
    ld l, a
    ld h, 0
    add hl, hl
    add hl, hl
    add hl, hl
    add hl, hl
    add hl, hl
    ld de, $9800
    add hl, de
    ld b, 32
    ld a, [wTile]
.col
    ld [hl+], a
    dec b
    jr nz, .col
    ret

SECTION "vars", WRAM0[$C000]
wCol:  db
wRR:   db
wBase: db
wTile: db
