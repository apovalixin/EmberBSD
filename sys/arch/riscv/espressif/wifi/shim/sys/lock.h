/* newlib-style locks, served by the kernel through the glue */
#ifndef ESPWIFI_SHIM_SYS_LOCK_H
#define ESPWIFI_SHIM_SYS_LOCK_H
typedef void *_lock_t;
void espwifi_os_lock_acquire(_lock_t *);
void espwifi_os_lock_release(_lock_t *);
#define _lock_acquire(l) espwifi_os_lock_acquire(l)
#define _lock_release(l) espwifi_os_lock_release(l)
#define _lock_init(l) (*(l) = 0)
#define _lock_close(l) ((void)0)
#endif
