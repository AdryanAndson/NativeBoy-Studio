; NativeBoy - translation tools test ROM (public domain)
; Texts use a custom encoding (A=$80..Z=$99, space=$7F, end=$50), like commercial games.
; The program decodes the two messages and sends them through the serial port, separated by "/".
; build: rgbasm -o tx.o text_test.asm && rgblink -o text_test.gb tx.o && rgbfix -v -p 0 text_test.gb

CHARMAP "A", $80
CHARMAP "B", $81
CHARMAP "C", $82
CHARMAP "D", $83
CHARMAP "E", $84
CHARMAP "F", $85
CHARMAP "G", $86
CHARMAP "H", $87
CHARMAP "I", $88
CHARMAP "J", $89
CHARMAP "K", $8A
CHARMAP "L", $8B
CHARMAP "M", $8C
CHARMAP "N", $8D
CHARMAP "O", $8E
CHARMAP "P", $8F
CHARMAP "Q", $90
CHARMAP "R", $91
CHARMAP "S", $92
CHARMAP "T", $93
CHARMAP "U", $94
CHARMAP "V", $95
CHARMAP "W", $96
CHARMAP "X", $97
CHARMAP "Y", $98
CHARMAP "Z", $99
CHARMAP " ", $7F
CHARMAP "@", $50

SECTION "entry", ROM0[$100]
    nop
    jp Main
    ds $150 - @, 0

SECTION "main", ROM0[$150]
Main:
    di
    ld sp, $FFFE
    ld hl, Msg1
    call PutStr
    ld a, $2F              ; "/"
    call Put
    ld hl, Msg2
    call PutStr
.h
    halt
    jr .h
PutStr:
    ld a, [hl+]
    cp $50
    ret z
    cp $7F
    jr nz, .letter
    ld a, $20              ; " "
    jr .out
.letter
    sub $80 - $41          ; byte -> ASCII letter
.out
    call Put
    jr PutStr
Put:
    ldh [$FF01], a
    ld a, $81
    ldh [$FF02], a
    ret

SECTION "texts", ROM0[$1000]
Msg1: db "HELLO WORLD@"
Msg2: db "PRESS START TO PLAY@"
