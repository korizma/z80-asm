#include "../common/glic80.h"

void main(void) {
    glic_prepare_screen(GLIC_BLACK);
    glic_fill_columns(43u, 42u, GLIC_WHITE);
}
