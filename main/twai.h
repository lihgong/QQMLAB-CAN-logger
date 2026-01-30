#ifndef __TWAI_H__
#define __TWAI_H__

#include <stdint.h>
#include <esp_err.h>

typedef struct twai_webui_status_s {
    uint32_t rx_pkt;
    uint32_t tx_pkt;
} twai_webui_status_t;

uint32_t twai_webui_query(twai_webui_status_t *p_stat);
esp_err_t twai_webui_transmit(uint32_t can_id, uint32_t data_len, uint8_t *p_data);

#endif // __TWAI_H__
