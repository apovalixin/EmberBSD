/* Optional diagnostics using the firmware SPCR UART address; no firmware calls. */
#ifndef _CM5TRACE_H_
#define _CM5TRACE_H_

#ifdef EFIBOOT_DIAGNOSTIC_UART
extern uintptr_t efi_cm5_uart_base;
#endif

static inline void
efi_cm5_trace(const char *message)
{
#ifdef EFIBOOT_DIAGNOSTIC_UART
	volatile uint32_t *uart = (volatile uint32_t *)efi_cm5_uart_base;
	unsigned timeout;

	if (efi_cm5_uart_base == 0)
		return;

	while (*message != '\0') {
		timeout = 10000;
		while ((uart[0x18 / 4] & 0x20) != 0) {
			if (--timeout == 0)
				return;
		}
		uart[0] = (unsigned char)*message++;
		__asm volatile("dsb sy" ::: "memory");
	}
#else
	(void)message;
#endif
}

static inline void
efi_cm5_pending(const char *stage)
{
#ifdef EFIBOOT_DIAGNOSTIC_UART
	static const char digits[] = "0123456789abcdef";
	uint64_t status;
	char value[19];
	unsigned i;

	__asm volatile("dsb sy; isb; mrs %0, isr_el1" : "=r"(status)
	    :: "memory");
	for (i = 0; i < 16; i++)
		value[i] = digits[(status >> ((15 - i) * 4)) & 15];
	value[16] = '\r';
	value[17] = '\n';
	value[18] = '\0';
	efi_cm5_trace("[CM5 EFI] ISR ");
	efi_cm5_trace(stage);
	efi_cm5_trace("=");
	efi_cm5_trace(value);
#else
	(void)stage;
#endif
}
#endif
