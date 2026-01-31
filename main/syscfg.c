#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "esp_log.h"
#include "ini.h"

static const char *TAG = "SYSCFG";

#define SYSCFG_INI "/sdcard/syscfg.ini"

// ----------
// SYSCFG REG
// ----------
#define SYSCFG_REG_FILE "syscfg_reg.h"

#define SYSCFG_REG(_section, _cb_func) extern uint32_t(_cb_func)(const char *section, const char *key, const char *value);
#include SYSCFG_REG_FILE
#undef SYSCFG_REG

typedef struct cfg_reg_s {
    char *section;
    uint32_t (*cb_func)(const char *section, const char *key, const char *value);
} cfg_reg_t;

cfg_reg_t cfg_tbl[] = {
#define SYSCFG_REG(_section, _cb_func) {.section = _section, .cb_func = _cb_func},
#include SYSCFG_REG_FILE
#undef SYSCFG_REG
};

// ----------
// syscfg
// ----------

static int syscfg_handler(void *user, const char *section, const char *name, const char *value)
{
    // FIXME: I think some configurations were system-side, and this handler should hijack them firstly

    for (uint32_t i = 0; i < sizeof(cfg_tbl) / sizeof(cfg_reg_t); i++) {
        cfg_reg_t *p_cfg = &cfg_tbl[i];
        if (strcmp(p_cfg->section, section) == 0) {
            return p_cfg->cb_func(section, name, value);
        }
    }
    return 1;
}

void syscfg_init(void)
{
    if (ini_parse(SYSCFG_INI, syscfg_handler, NULL) < 0) {
        ESP_LOGE(TAG, "Can't load " SYSCFG_INI);
    }
}
