; NativeBoy - widescreen test ROM: diagonal scrolling with whole map rows (public domain)
; Scrolls right 1 pixel per frame and down 1 pixel every other frame, like a title screen flying over a map.
; World tile (cx, cy) is Table[(cx + 2*cy) & 255] (low 8 bits of the world tile coordinates).
; Like many games it only streams what is about to appear: a new column (19 tiles) at the right edge and a new
; WHOLE map row (32 tiles, world columns cx0-6 .. cx0+25) at the bottom edge. Writing whole rows touches every
; map column, while the other rows of those columns still hold tiles of places already passed.
; build: rgbasm -o dg.o diag_test.asm && rgblink -o diag_test.gb dg.o && rgbfix -v -p 0 diag_test.gb

DEF rLCDC EQU $FF40
DEF rSCY  EQU $FF42
DEF rSCX  EQU $FF43
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
    ; tiles 1..15: distinct patterns (rows alternate two bytes derived from k)
    ld hl, $8010
    ld b, 1
.tiles
    ld a, b
    ld c, a
    add a
    add a
    add a
    add c
    ld d, a          ; k*9
    ld a, c
    swap a
    xor c
    ld e, a
    ld c, 4
.trow
    ld a, d
    ld [hl+], a
    ld a, e
    ld [hl+], a
    ld a, e
    ld [hl+], a
    ld a, d
    ld [hl+], a
    dec c
    jr nz, .trow
    inc b
    ld a, b
    cp 16
    jr nz, .tiles
    xor a
    ld [wX], a
    ld [wX+1], a
    ld [wY], a
    ld [wY+1], a
    ld [wFrame], a
    ; whole visible area (and the wide sides) at start: rows 0..18, columns -6..25
    ld c, 0
.irow
    push bc
    call WriteRow
    pop bc
    inc c
    ld a, c
    cp 19
    jr nz, .irow
    xor a
    ldh [rSCX], a
    ldh [rSCY], a
    ld a, %10010001          ; LCD on, tiles at $8000, BG on
    ldh [rLCDC], a
    ld a, 1
    ldh [rIE], a
    ei
Loop:
    halt
    ; new scroll first (still in vertical blank), then the map is streamed
    ; X += 1
    ld hl, wX
    inc [hl]
    jr nz, .xok
    inc hl
    inc [hl]
.xok
    ; Y += 1 every other frame
    ld hl, wFrame
    inc [hl]
    ld a, [hl]
    and 1
    jr nz, .noy
    ld hl, wY
    inc [hl]
    jr nz, .noy
    inc hl
    inc [hl]
.noy
    ld a, [wX]
    ldh [rSCX], a
    ld a, [wY]
    ldh [rSCY], a
    ld a, [wX]
    and 7
    jr nz, .nocol
    call WriteColumn
.nocol
    ld a, [wFrame]
    and 1
    jr nz, Loop
    ld a, [wY]
    and 7
    jr nz, Loop
    ; new row just below the screen: world row cy0 + 18
    call CamTileY
    add 18
    ld c, a
    call WriteRow
    jr Loop

; a = low 8 bits of X >> 3 (world tile column of the camera)
CamTileX:
    ld a, [wX+1]
    ld b, a
    ld a, [wX]
    srl b
    rra
    srl b
    rra
    srl b
    rra
    ret
CamTileY:
    ld a, [wY+1]
    ld b, a
    ld a, [wY]
    srl b
    rra
    srl b
    rra
    srl b
    rra
    ret

; c = world row (low 8 bits): writes world columns cx0-6 .. cx0+25 (32 tiles)
WriteRow:
    call CamTileX
    sub 6
    ld b, a
    ld a, 32
.r
    push af
    call PutTile
    inc b
    pop af
    dec a
    jr nz, .r
    ret

; column just right of the screen: world column cx0 + 20, rows cy0 .. cy0 + 18
WriteColumn:
    call CamTileY
    ld c, a
    call CamTileX
    add 20
    ld b, a
    ld a, 19
.c
    push af
    call PutTile
    inc c
    pop af
    dec a
    jr nz, .c
    ret

; b = world column, c = world row (low 8 bits) -> map ($9800) tile Table[(b + 2c) & 255]
PutTile:
    ld a, c
    and 31
    ld l, a
    ld h, 0
    add hl, hl
    add hl, hl
    add hl, hl
    add hl, hl
    add hl, hl
    ld a, b
    and 31
    or l
    ld l, a
    ld a, h
    add $98
    ld h, a
    ld a, c
    add a
    add b
    ld e, a
    ld d, HIGH(Table)
    ld a, [de]
    ld [hl], a
    ret

SECTION "table", ROM0, ALIGN[8]
Table:
FOR I, 256
    db (I % 15) + 1
ENDR

SECTION "vars", WRAM0[$C000]
wX:     dw
wY:     dw
wFrame: db
