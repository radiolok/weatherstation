/* Iterable section of the HTTP resources of the ws_http service (web.c):
 * HTTP_RESOURCE_DEFINE places them here, the linker script must know it. */
#include <zephyr/linker/iterable_sections.h>

ITERABLE_SECTION_ROM(http_resource_desc_ws_http, Z_LINK_ITERABLE_SUBALIGN)
