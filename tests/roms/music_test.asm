; NativeBoy - soundtrack replacement test ROM (public domain)
; Keeps the "current song" number at $C0F0: 1 for ~1 s, then 2 for ~1 s, then $FF (no song),
; playing a square tone on channel 1 all along. Serial "D" when done.
; build: rgbasm -o mu.o music_test.asm && rgblink -o music_test.gb mu.o && rgbfix -v -p 0 music_test.gb

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
    ld a, $80
    ldh [$FF26], a
    ld a, $77
    ldh [$FF24], a
    ld a, $FF
    ldh [$FF25], a
    ld a, $80
    ldh [$FF11], a
    ld a, $F0
    ldh [$FF12], a
    ld a, $00
    ldh [$FF13], a
    ld a, $87
    ldh [$FF14], a
    ld a, 1
    ldh [$FFFF], a
    ld a, $91
    ldh [$FF40], a
    ei
    ld a, 1
    ld [$C0F0], a
    ld c, 60
    call Frames
    ld a, 2
    ld [$C0F0], a
    ld c, 60
    call Frames
    ld a, $FF
    ld [$C0F0], a
    ld a, "D"
    ldh [$FF01], a
    ld a, $81
    ldh [$FF02], a
.h
    halt
    jr .h
Frames:
    halt
    dec c
    jr nz, Frames
    ret
