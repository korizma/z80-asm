#!/bin/sh
set -u

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
OUT="$ROOT/tests/out"
ASM="$ROOT/z80-asm"

rm -rf "$OUT"
mkdir -p "$OUT"

failures=0
checks=0

write_hex() {
    python3 - "$1" "$2" <<'PY'
from pathlib import Path
import sys
Path(sys.argv[1]).write_bytes(bytes.fromhex(sys.argv[2]))
PY
}

check_bin() {
    name=$1
    expected_hex=$2
    asm_file="$ROOT/tests/fixtures/$name.asm"
    actual="$OUT/$name.bin"
    expected="$OUT/$name.expected"
    checks=$((checks + 1))
    write_hex "$expected" "$expected_hex"
    if "$ASM" -o "$actual" "$asm_file" >/dev/null 2>"$OUT/$name.err" &&
       cmp -s "$expected" "$actual"; then
        printf 'ok   %s\n' "$name"
    else
        printf 'FAIL %s\n' "$name"
        if [ -s "$OUT/$name.err" ]; then
            sed 's/^/     /' "$OUT/$name.err"
        fi
        printf '     expected: %s\n' "$expected_hex"
        if [ -f "$actual" ]; then
            actual_hex=$(python3 - "$actual" <<'PY'
from pathlib import Path
import sys
print(Path(sys.argv[1]).read_bytes().hex())
PY
)
            printf '     actual:   %s\n' "$actual_hex"
        else
            printf '     actual:   <missing>\n'
        fi
        failures=$((failures + 1))
    fi
}

check_failure() {
    name=$1
    asm_file="$ROOT/tests/fixtures/$name.asm"
    actual="$OUT/$name.bin"
    checks=$((checks + 1))
    if "$ASM" -o "$actual" "$asm_file" >/dev/null 2>"$OUT/$name.err"; then
        printf 'FAIL %s unexpectedly succeeded\n' "$name"
        failures=$((failures + 1))
    elif [ -e "$actual" ]; then
        printf 'FAIL %s left a partial output file\n' "$name"
        failures=$((failures + 1))
    else
        printf 'ok   %s\n' "$name"
    fi
}

check_bin core "3e1247808ede349e91dda602fdb6ffee55b820fd10ea"
check_bin load "dd7705dd36069afd4efe2234122a3412ed4b3412dd217856fd2a0020ddf9ed57ed4f021aed7b1111"
check_bin cb_ed "cb5fcbfeddcb0186cb11fdcbfe3eed7aed52edb0ed78db44ed69d355ffed5eed6fed67"
check_bin flow "c30900cc0900d0180100cd0900c9"
check_bin directives "415a01ff0a0001341200ee113322444448691201"
check_bin fixed "08ebdde3d93f37272f17071f0fed4ded45ed4476"
check_bin compat "18064869000000003e051800c30c000000"
check_bin ds_labels "210700000000000700"
check_bin sdcc_compat "dd210000dd5e05fd6e063e07e60393b3ee01fe0628023effc918003e00c9"

checks=$((checks + 1))
if "$ASM" -o "$OUT/test_main_z80.bin" "$ROOT/test_main_z80.asm" >/dev/null 2>"$OUT/test_main_z80.err" &&
   [ -s "$OUT/test_main_z80.bin" ]; then
    printf 'ok   test_main_z80\n'
else
    printf 'FAIL test_main_z80\n'
    if [ -s "$OUT/test_main_z80.err" ]; then
        sed 's/^/     /' "$OUT/test_main_z80.err"
    fi
    failures=$((failures + 1))
fi

if command -v sdcc-sdcc >/dev/null 2>&1 || command -v sdcc >/dev/null 2>&1; then
    checks=$((checks + 1))
    if "$ROOT/tools/sdcc-z80-bin" -o "$OUT/test_main_tool.bin" "$ROOT/c_programs/test_main.c" \
        >/dev/null 2>"$OUT/test_main_tool.err"; then
        prefix=$(python3 - "$OUT/test_main_tool.bin" <<'PY'
from pathlib import Path
import sys
print(Path(sys.argv[1]).read_bytes()[:15].hex())
PY
)
        case "$prefix" in
            180a746573745f6d61696e00310077)
                printf 'ok   sdcc-z80-bin\n'
                ;;
            *)
                printf 'FAIL sdcc-z80-bin\n'
                printf '     prefix: %s\n' "$prefix"
                failures=$((failures + 1))
                ;;
        esac
    else
        printf 'FAIL sdcc-z80-bin\n'
        if [ -s "$OUT/test_main_tool.err" ]; then
            sed 's/^/     /' "$OUT/test_main_tool.err"
        fi
        failures=$((failures + 1))
    fi
else
    printf 'skip sdcc-z80-bin (sdcc not found)\n'
fi

cat >"$OUT/default.asm" <<'ASMEOF'
NOP
ASMEOF
write_hex "$OUT/default.expected" "00"
checks=$((checks + 1))
if (cd "$OUT" && "$ASM" default.asm >/dev/null 2>default.err) &&
   cmp -s "$OUT/default.expected" "$OUT/default.bin"; then
    printf 'ok   default-output\n'
else
    printf 'FAIL default-output\n'
    failures=$((failures + 1))
fi

check_failure duplicate_label
check_failure undefined_symbol
check_failure relative_range
check_failure invalid_operand
check_failure byte_range
check_failure backward_org

if [ "$failures" -ne 0 ]; then
    printf '%d/%d checks failed\n' "$failures" "$checks"
    exit 1
fi

printf '%d checks passed\n' "$checks"
