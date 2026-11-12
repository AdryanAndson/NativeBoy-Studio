; NativeBoy - jump table through HL test ROM (public domain): like jt_test, but the table is
; read inline ("ld hl,Table / add / ld a,[hl+] / ld h,[hl] / ld l,a / jp hl") instead of via rst.
; A state machine dispatched through "rst $00" + an inline pointer table,
; the classic pattern of commercial games. Serial output: "ABCDE" then "OK".
; State E is reached only through the table entry, never by a direct jump.
; build: rgbasm -o jt2.o jt2_test.asm && rgblink -o jt2_test.gb jt2.o && rgbfix -v -p 0 jt_test.gb

DEF rSB EQU $FF01
DEF rSC EQU $FF02

SECTION "rst00", ROM0[$0000]
JumpTable:            ; a = index, table follows the "rst $00"
    add a
    pop hl
    ld e, a
    ld d, 0
    add hl, de
    ld a, [hl+]
    ld h, [hl]
    ld l, a
    jp hl

SECTION "entry", ROM0[$100]
    nop
    jp Main
    ds $150 - @, 0

SECTION "main", ROM0[$150]
Main:
    di
    ld sp, $FFFE
    xor a
    ld [wState], a
.loop
    ld a, [wState]
    add a
    ld e, a
    ld d, 0
    ld hl, Table
    add hl, de
    ld a, [hl+]
    ld h, [hl]
    ld l, a
    jp hl
Table:
    dw StateA, StateB, StateC, StateD, StateE, StateDone

StateA:
    ld a, "A"
    jr Next
StateB:
    ld a, "B"
    jr Next
StateC:
    ld a, "C"
    jr Next
StateD:
    ld a, "D"
    jr Next
StateE:
    ld a, [wCounter]
    inc a
    ld [wCounter], a
    ld a, "E"
Next:
    call PutChar
    ld hl, wState
    inc [hl]
    jr Main.loop

StateDone:
    ld a, "O"
    call PutChar
    ld a, "K"
    call PutChar
.halt
    halt
    jr .halt

PutChar:
    ldh [rSB], a
    ld a, $81
    ldh [rSC], a
    ret

SECTION "vars", WRAM0[$C100]
wState:   db
wCounter: db
