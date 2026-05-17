    JR code
    DB %01001000, %01101001
    FORG 8

code:
value: EQU 5
    LD A,value
    JR .done
.done:
    JP .done
    DS 2
