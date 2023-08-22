#ifndef __BSP_WDT_H__
#define __BSP_WDT_H__

#ifdef __cplusplus
extern "C" {
#endif

void bsp_wdt_init(void);
void bsp_wdt_feed(void);

#ifdef __cplusplus
}
#endif

#endif // __BSP_WDT_H__
