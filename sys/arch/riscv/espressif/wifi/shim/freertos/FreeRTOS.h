/* Stand-in for the FreeRTOS headers: the ESP-IDF sources built for NetBSD
 * take their critical sections from the kernel. */
#ifndef ESPWIFI_SHIM_FREERTOS_H
#define ESPWIFI_SHIM_FREERTOS_H
#include <stdint.h>
#include <stdbool.h>
#include "spinlock.h"
typedef spinlock_t portMUX_TYPE;

#define portMUX_INITIALIZER_UNLOCKED SPINLOCK_INITIALIZER

uint32_t espwifi_os_critical_enter(void);
void espwifi_os_critical_exit(uint32_t);
#define portENTER_CRITICAL(mux) ((void)(mux), (void)espwifi_os_critical_enter())
#define portEXIT_CRITICAL(mux) ((void)(mux), espwifi_os_critical_exit(0))
#define portENTER_CRITICAL_SAFE(mux) portENTER_CRITICAL(mux)
#define portEXIT_CRITICAL_SAFE(mux) portEXIT_CRITICAL(mux)
#define portENTER_CRITICAL_ISR(mux) portENTER_CRITICAL(mux)
#define portEXIT_CRITICAL_ISR(mux) portEXIT_CRITICAL(mux)
int espwifi_os_in_isr(void);
#define xPortInIsrContext() espwifi_os_in_isr()
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
typedef void *SemaphoreHandle_t;
#define portMAX_DELAY 0xffffffffU
#define portTICK_PERIOD_MS 10
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#endif
