#ifndef __SYSCFG_H__
#define __SYSCFG_H__

typedef struct syscfg_system_s {
    char hostname[64]; // RFC 1035, include \0
} syscfg_system_t;

const syscfg_system_t *syscfg_system_p(void);

#endif // __SYSCFG_H__