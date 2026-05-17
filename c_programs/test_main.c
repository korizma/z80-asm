#include "common/glic80.h"

void main(void) {
    unsigned int i;

    glic_clear_text();

    for (i = 0; i < GLIC_GVRAM_BYTES; ++i) {
        GLIC_GVRAM[i] = (i & 1u) ? GLIC_BLACK : GLIC_WHITE;
    }
}
