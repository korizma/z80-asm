#include "../common/glic80.h"

void main(void) {
    glic_prepare_screen(GLIC_BLACK);

    glic_fill_columns(40u, 16u, GLIC_WHITE);
    glic_fill_pages(6u, 4u, GLIC_WHITE);
}
