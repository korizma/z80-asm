ORG 0x100

start:
    DB "AZ", 1, -1, '\n'
    DW start, 0x1234
ORG 0x10A
    DB 0xEE
    .db 0x11
    .dw 0x2233
    .ds 2,0x44
    .ascii "H","i"
addr_test:
    .db <addr_test, >addr_test
