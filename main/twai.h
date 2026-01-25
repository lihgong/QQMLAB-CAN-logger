#ifndef __TWAI_H__
#define __TWAI_H__

#include <stdint.h>

typedef struct twai_webui_status_s {
    uint32_t rx_pkt;
    uint32_t tx_pkt;
} twai_webui_status_t;

uint32_t twai_webui_query(twai_webui_status_t *p_stat);

#endif // __TWAI_H__
