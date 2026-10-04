/* $NetBSD$ */

#ifndef _RISCV_ESPRESSIF_ESP_WIFI_VAR_H_
#define _RISCV_ESPRESSIF_ESP_WIFI_VAR_H_

void	espwifi_attach(void);
void	espwifi_if_attach(void);
void	espwifi_if_input(const void *, unsigned int);
void	espwifi_if_link(int);

#endif
