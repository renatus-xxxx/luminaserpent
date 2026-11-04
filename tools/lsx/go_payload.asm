; GO.COM payload: self-relocated loader for LSX-Dodgers.
; Runs at C300h - above LUMINA.COM's image end (C227h) and below the
; LSX-Dodgers kernel base (BDOS entry CB06h, so kernel occupies CB06h+).
; Loads LUMINA.COM via BDOS FCB sequential reads, buffered through a
; local 128-byte buffer, to 0100h, then jumps to it.
; Needed because the LSX CCP cannot start a ~49KB program from
; AUTOEXEC.BAT (fails with "Error!"), while small programs load fine.
        org     0C300h
        ld      sp, stack_top
        ld      de, fcb
        ld      c, 0Fh          ; Open
        call    0005h
        inc     a
        ret     z               ; open failed: back to CCP
        ld      hl, 0100h
        ld      (dest), hl
loop:   ld      de, buffer
        ld      c, 1Ah          ; Set DMA
        call    0005h
        ld      de, fcb
        ld      c, 14h          ; Sequential read (128 bytes)
        call    0005h
        or      a
        jr      nz, done        ; EOF
        ld      hl, buffer
        ld      de, (dest)
        ld      bc, 128
        ldir
        ld      (dest), de
        jr      loop
done:   jp      0100h
dest:   dw      0
fcb:    db      1               ; drive A:
        db      "LUMINA  COM"
        ds      25, 0
buffer: ds      128, 0
        ds      64, 0
stack_top:
