#include "../common/glic80.h"

void main(void) {
    glic_prepare_screen(GLIC_BLACK);
    glic_fill_pages(5u, 6u, GLIC_WHITE);
}
