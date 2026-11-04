; GO.COM stage 1: loaded by the LSX CCP at 0100h.
; Copies the payload to C300h and jumps to it.
        org     0100h
start:  ld      hl, payload
        ld      de, 0C300h
        ld      bc, payload_end - payload
        ldir
        jp      0C300h
payload:
        incbin  "go_payload.bin"
payload_end:
