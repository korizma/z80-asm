#include "../common/glic80.h"

void main(void) {
    glic_prepare_screen(GLIC_BLACK);

    glic_fill_page_run(5u, 48u, 32u, GLIC_WHITE);
    glic_fill_page_run(6u, 48u, 32u, GLIC_WHITE);
    glic_fill_page_run(7u, 32u, 64u, GLIC_WHITE);
    glic_fill_page_run(8u, 32u, 64u, GLIC_WHITE);
    glic_fill_page_run(9u, 48u, 32u, GLIC_WHITE);
    glic_fill_page_run(10u, 48u, 32u, GLIC_WHITE);
}
