; NativeBoy - animated scenery test ROM (public domain): scroll_test + tile 3 changes every frame
; Scrolls right 1 pixel per frame. The map column that leaves the screen is cleared at once. Like many games, it keeps re-drawing only the columns that
; are on screen (3 per frame, round robin), so map columns that went off-screen become stale.
; World column c uses tile (c mod 15) + 1. A window HUD bar covers the last 8 lines.
; build: rgbasm -o sc.o scroll_test.asm && rgblink -o scroll_test.gb sc.o && rgbfix -v -p 0 scroll_test.gb

DEF rLCDC EQU $FF40
DEF rSCX  EQU $FF43
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
    ; tiles 1..15: vertical stripe patterns (lo = k*37, hi = k*91), tile 16: solid HUD line
    ld hl, $8010
    ld b, 1
.tiles
    ld a, b
    ld c, a
    add a
    add a
    add a
    add c            ; a = k*9 (cheap distinct pattern)
    ld d, a
    ld a, c
    swap a
    xor c
    ld e, a          ; e = (k<<4|k>>4) ^ k
    ld c, 8
.trow
    ld a, d
    ld [hl+], a
    ld a, e
    ld [hl+], a
    dec c
    jr nz, .trow
    inc b
    ld a, b
    cp 17
    jr nz, .tiles
    ; HUD tile 16 (overwrite last): top row solid, rest light
    ld hl, $8100
    ld a, $FF
    ld [hl+], a
    ld [hl+], a
    ld c, 14
.hud
    xor a
    ld [hl+], a
    dec c
    jr nz, .hud
    ; window map row 0 = HUD tile
    ld hl, $9C00
    ld c, 32
.wrow
    ld a, 16
    ld [hl+], a
    dec c
    jr nz, .wrow
    ; world columns 0..31 drawn at start (whole map)
    xor a
    ld [wCol], a
.init
    ld a, [wCol]
    call DrawColumn
    ld hl, wCol
    inc [hl]
    ld a, [hl]
    cp 32
    jr nz, .init
    xor a
    ld [wRR], a
    ldh [rSCX], a
    ld a, 136
    ldh [rWY], a
    ld a, 7
    ldh [rWX], a
    ld a, %11110011          ; LCD on, window on (map 9C00), tiles at $8000, BG on, sprites on
    ldh [rLCDC], a
    ld a, 1
    ldh [rIE], a
    ei
Loop:
    halt
    ld hl, $8030             ; animated tile: every byte of tile 3 is inverted each frame
    ld b, 16
.anim
    ld a, [hl]
    cpl
    ld [hl+], a
    dec b
    jr nz, .anim
    ldh a, [rSCX]
    inc a
    ldh [rSCX], a
    ; redraw 3 on-screen columns per frame: first visible column = scx/8, range 0..20
    ld b, 3
.redraw
    push bc
    ld a, [wRR]
    inc a
    cp 21
    jr c, .ok
    xor a
.ok
    ld [wRR], a
    ld b, a
    ldh a, [rSCX]
    srl a
    srl a
    srl a
    add b                     ; screen column -> map column (mod 32 inside DrawColumn)
    ld c, a
    ; world column = (pixel scroll total / 8) + rr ; kept as wBase + c with 8-bit wrap
    ld a, [wBase]
    add c
    call DrawColumnAt         ; a = world column (low 8 bits), c = map column
    pop bc
    dec b
    jr nz, .redraw
    ; the column that just left the screen is reused (cleared), as games do with off-screen map space
    ldh a, [rSCX]
    srl a
    srl a
    srl a
    dec a
    ld c, a
    xor a
    ld [wTile], a
    call ClearColumn
    ; every 256 pixels the 8-bit SCX wraps: advance the world base by 32 columns
    ldh a, [rSCX]
    and a
    jr nz, Loop
    ld a, [wBase]
    add 32
    ld [wBase], a
    jr Loop

; a = world column (also map column for the initial draw)
DrawColumn:
    ld c, a
DrawColumnAt:            ; a = world column, c = map column
.mod
    cp 15
    jr c, .done
    sub 15
    jr .mod
.done
    inc a
    ld [wTile], a
ClearColumn:             ; writes [wTile] into map column c
    ld a, c
    and 31
    ld l, a
    ld h, $98
    ld de, 32
    ld b, 18
.col
    ld a, [wTile]
    ld [hl], a
    add hl, de
    dec b
    jr nz, .col
    ret

SECTION "vars", WRAM0[$C000]
wCol:  db
wRR:   db
wBase: db
wTile: db
