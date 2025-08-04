#ifndef LF_125KHZ_RADIO_H_
#define LF_125KHZ_RADIO_H_

void lf_125khz_radio_gpiote_init(void);
void lf_125khz_radio_saadc_init(void);
void lf_125khz_radio_uninit(void);
void start_lf_125khz_radio(void);
void stop_lf_125khz_radio(void);

#endif
