ORG 0

start:
    JP target
    CALL Z,target
    RET NC
    JR skip
target:
    NOP
skip:
    CALL target
    RET
