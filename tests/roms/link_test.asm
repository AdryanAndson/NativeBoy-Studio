; NativeBoy - link cable test ROM (public domain)
; Hold A at power on = drives the clock (sends "HELLO"); otherwise answers "01234".
; Received bytes are stored at $C000-$C004 and $C010 = $01 when finished.
; build: rgbasm -o l.o link_test.asm && rgblink -o link_test.gb l.o && rgbfix -v -p 0 link_test.gb

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
    xor a
    ld [$C010], a
    ld a, 1
    ldh [$FFFF], a
    ld a, $91
    ldh [$FF40], a
    ei
    ld a, $10              ; select the button keys
    ldh [$FF00], a
    ldh a, [$FF00]
    ldh a, [$FF00]
    bit 0, a
    jr z, Master
Slave:
    ld hl, $C000
    ld b, "0"
.next
    ld a, b
    ldh [$FF01], a
    ld a, $80
    ldh [$FF02], a
.wait
    ldh a, [$FF02]
    bit 7, a
    jr nz, .wait
    ldh a, [$FF01]
    ld [hl+], a
    inc b
    ld a, b
    cp "5"
    jr nz, .next
    jr Done
Master:
    ld c, 30
    call Frames
    ld hl, $C000
    ld de, Hello
.next
    ld a, [de]
    and a
    jr z, Done
    inc de
    ldh [$FF01], a
    ld a, $81
    ldh [$FF02], a
.wait
    ldh a, [$FF02]
    bit 7, a
    jr nz, .wait
    ldh a, [$FF01]
    ld [hl+], a
    ld c, 2
    call Frames
    jr .next
Done:
    ld a, 1
    ld [$C010], a
.h
    halt
    jr .h
Frames:
    halt
    dec c
    jr nz, Frames
    ret
Hello:
    db "HELLO", 0
