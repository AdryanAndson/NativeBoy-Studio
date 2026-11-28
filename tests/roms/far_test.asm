; NativeBoy - indirect calls test ROM (public domain).
; The functions below are reached ONLY through the helper routines most games use:
;   A: "ld hl, Func / call CallHL"            (CallHL is a lone "jp hl")
;   B: "ld a, BANK / ld hl, Func / call FarCall"  (switches the bank, calls hl, switches back)
;   C: "ld b, BANK / ld hl, Func / rst $08"        (bank in B, the rst jumps to hl)
;   D: inside bank 2, "ld hl, Func / call CallHL" to a function of the same bank
;   E: "ld de, Func / push de / ret"               (return-address trick)
; The static analysis must find every one of them without playing (discovery off).
; Serial output: "ABCDE" then "OK".
; build: rgbasm -o far.o far_test.asm && rgblink -o far_test.gb far.o && rgbfix -v -p 0 -m 1 -r 0 far_test.gb

DEF rSB EQU $FF01
DEF rSC EQU $FF02

SECTION "rst08", ROM0[$0008]
RstFar:               ; b = bank, hl = function
    ld a, b
    ld [$2000], a
    jp hl

SECTION "entry", ROM0[$100]
    nop
    jp Main
    ds $150 - @, 0

SECTION "main", ROM0[$150]
Main:
    di
    ld sp, $FFFE
    ld hl, FuncA
    call CallHL
    ld a, BANK(FuncB)
    ld hl, FuncB
    call FarCall
    ld b, BANK(FuncC)
    ld hl, FuncC
    rst $08
    ld a, BANK(FuncD0)
    ld hl, FuncD0
    call FarCall
    ld de, .after
    push de
    ld de, FuncE
    push de
    ret
.after
    ld a, "O"
    call PutChar
    ld a, "K"
    call PutChar
.halt
    halt
    jr .halt

CallHL:
    jp hl

FarCall:              ; a = bank, hl = function; bank 1 again on return
    ld [$2000], a
    call CallHL
    ld a, 1
    ld [$2000], a
    ret

FuncA:
    ld a, "A"
    jp PutChar

FuncE:
    ld a, "E"
    jp PutChar

PutChar:
    ld [rSB], a
    ld a, $81
    ld [rSC], a
.wait
    ld a, [rSC]
    bit 7, a
    jr nz, .wait
    ret

SECTION "bank1", ROMX, BANK[1]
    db "bank 1"

SECTION "bank2", ROMX, BANK[2]
FuncB:
    ld a, "B"
    jp PutChar
FuncD0:
    ld hl, FuncD
    call CallHL
    ret
FuncD:
    ld a, "D"
    jp PutChar

SECTION "bank3", ROMX, BANK[3]
FuncC:
    ld a, "C"
    jp PutChar
