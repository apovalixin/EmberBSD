#!/usr/bin/env python3
"""Compile the actual BCM2712 attachment against controlled kernel boundaries.

The fixtures catch MMIO on another board, loss of an active IRQ after a busy
unload, and successful load despite IRQ failure. No hardware is accessed.
"""
import pathlib
import subprocess
import sys
import tempfile

source = pathlib.Path(sys.argv[1]).read_text()
source = '\n'.join(line for line in source.splitlines() if not line.startswith('#include'))
prefix = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct fake_device *device_t;
typedef unsigned bus_space_tag_t;
typedef unsigned bus_space_handle_t;
typedef int modcmd_t;
struct fake_device { void *private; };
struct acpi_softc { bus_space_tag_t sc_memt; };
static struct acpi_softc acpi = { 1 }, *acpi_softc = &acpi;
struct com_regs { unsigned tag, handle; };
struct com_softc { device_t sc_dev; int sc_type, sc_frequency, sc_hwflags; struct com_regs sc_regs; };
struct cfdata { const char *cf_name, *cf_atname; int cf_fstate, cf_unit; };
struct cfdriver { int cd_ndevs; };
struct cfattach { size_t size; void (*attach)(device_t,device_t,void*); int (*detach)(device_t,int); };
static struct cfdriver driver = { 1 };
static struct cfattach *attachment;
static struct fake_device device;
static unsigned mappings, unmaps, irq_active, com_attaches, com_detaches;
static int detach_error, fail_irq;
static const char *product = "Raspberry Pi 5 Model B";
static uint32_t mux = 0x44000000, pull = 0xabcdef01, data = 0x10000000, direction = 0xffffffff;
#define MODULE(a,b,c)
#define MODULE_CLASS_DRIVER 0
#define MODULE_CMD_INIT 1
#define MODULE_CMD_FINI 2
#define FSTATE_STAR 0
#define COM_TYPE_NORMAL 0
#define COM_HW_AFE 1
#define IPL_SERIAL 0
#define IST_LEVEL 0
#define CFATTACH_DECL_NEW(name,sz,match,attach,detach,activate) \
 struct cfattach name##_ca = {sz,attach,detach}
#define aprint_error_dev(...) ((void)0)
#define aprint_normal_dev(...) ((void)0)
static const char *pmf_get_platform(const char *key) { (void)key; return product; }
static void *device_private(device_t dev) { return dev->private; }
static const char *device_xname(device_t dev) { (void)dev; return "com0"; }
static int bus_space_map(unsigned tag, uint64_t addr, size_t size, int flags, unsigned *handle) {
 (void)tag;(void)size;(void)flags;mappings++;
 *handle = addr == 0x107d50c000ULL ? 1 : addr == 0x107d504000ULL ? 2 : 3;
 return 0;
}
static void bus_space_unmap(unsigned tag,unsigned handle,size_t size) { (void)tag;(void)handle;(void)size;unmaps++; }
static uint32_t bus_space_read_4(unsigned tag,unsigned handle,unsigned offset) {
 (void)tag;
 if(handle==2 && offset==0x10c)return mux;
 if(handle==2 && offset==0x110)return 0x3434;
 if(handle==2 && offset==0x124)return pull;
 if(handle==3 && offset==0x504)return data;
 if(handle==3 && offset==0x508)return direction;
 return 0;
}
static void bus_space_write_4(unsigned tag,unsigned handle,unsigned offset,uint32_t value) {
 (void)tag;
 if(handle==2 && offset==0x10c)mux=value;
 if(handle==2 && offset==0x124)pull=value;
 if(handle==3 && offset==0x504)data=value;
 if(handle==3 && offset==0x508)direction=value;
}
static void delay(unsigned microseconds) { (void)microseconds; }
static struct cfdriver *config_cfdriver_lookup(const char *name) { (void)name;return &driver; }
static device_t device_lookup(struct cfdriver *d,int unit) { (void)d;(void)unit;return NULL; }
static int config_cfattach_attach(const char *name,struct cfattach *ca) { (void)name;attachment=ca;return 0; }
static int config_cfattach_detach(const char *name,struct cfattach *ca) { (void)name;(void)ca;return 0; }
static device_t config_attach_pseudo(struct cfdata *cf) {
 (void)cf;device.private=calloc(1,attachment->size);assert(device.private);
 attachment->attach(NULL,&device,NULL);return &device;
}
static int config_detach(device_t dev,int flags) {
 int e=attachment->detach(dev,flags);if(!e){free(dev->private);dev->private=NULL;}return e;
}
static void com_init_regs_stride_width(struct com_regs *r,unsigned tag,unsigned handle,uint64_t addr,int shift,int width) {
 (void)addr;assert(shift==2 && width==4);r->tag=tag;r->handle=handle;
}
static void com_attach_subr(struct com_softc *sc) { (void)sc;com_attaches++; }
static int com_detach(device_t dev,int flags) { (void)dev;(void)flags;com_detaches++;return detach_error; }
static void comintr(void *arg) { (void)arg; }
static void *intr_establish_xname(int irq,int level,int type,void(*fn)(void*),void *arg,const char *name) {
 (void)level;(void)type;(void)fn;(void)arg;(void)name;assert(irq==308);
 if(fail_irq)return NULL;irq_active=1;return &irq_active;
}
static void intr_disestablish(void *handle) { assert(handle);irq_active=0; }
static int com_resume(device_t dev,const void *q) { (void)dev;(void)q;return 1; }
static int pmf_device_register(device_t dev,void *s,int(*r)(device_t,const void*)) { (void)dev;(void)s;(void)r;return 1; }
static void pmf_device_deregister(device_t dev) { (void)dev; }
'''
suffix = r'''
static void reset(void) {
 mappings=unmaps=irq_active=com_attaches=com_detaches=0;
 detach_error=fail_irq=0;product="Raspberry Pi 5 Model B";
 mux=0x44000000;pull=0xabcdef01;data=0x10000000;direction=0xffffffff;
}
int main(void) {
 int failures=0, e;
 reset();product="QEMU Virtual Machine";
 e=ATTACH_MODCMD(MODULE_CMD_INIT,NULL);
 if(e==ENXIO && mappings==0)puts("another board avoids all MMIO: PASS");
 else {puts("another board avoids all MMIO: FAIL");failures++;if(!e)ATTACH_MODCMD(MODULE_CMD_FINI,NULL);}
 reset();e=ATTACH_MODCMD(MODULE_CMD_INIT,NULL);
 assert(!e);assert(irq_active);assert(com_attaches==1);
 if((data & (1U<<28)) && (direction & (1U<<28)))puts("Wi-Fi power and direction preserved: PASS");
 else {puts("Wi-Fi power and direction preserved: FAIL");failures++;}
 detach_error=EBUSY;e=ATTACH_MODCMD(MODULE_CMD_FINI,NULL);
 if(e==EBUSY && irq_active && unmaps==0)puts("busy unload preserves active IRQ: PASS");
 else {puts("busy unload preserves active IRQ: FAIL");failures++;}
 detach_error=0;e=ATTACH_MODCMD(MODULE_CMD_FINI,NULL);assert(!e);
 if(!irq_active && unmaps==3 && mux==0x44000000 && pull==0xabcdef01 && data==0x10000000 && direction==0xffffffff)
  puts("unload restores only owned pins: PASS");
 else {puts("unload restores only owned pins: FAIL");failures++;}
 reset();fail_irq=1;e=ATTACH_MODCMD(MODULE_CMD_INIT,NULL);
 if(e!=0 && unmaps==3 && !com_attaches && mux==0x44000000 && data==0x10000000)
  puts("IRQ failure unwinds load: PASS");
 else {puts("IRQ failure unwinds load: FAIL");failures++;if(!e)ATTACH_MODCMD(MODULE_CMD_FINI,NULL);}
 return failures!=0;
}
'''
name = 'bcm2712btcom' if 'bcm2712btcom_modcmd' in source else 'bcmuartprobe'
with tempfile.TemporaryDirectory(prefix='bluetooth-uart-contract-') as directory:
    path = pathlib.Path(directory) / 'contract.c'
    path.write_text(prefix + source + suffix.replace('ATTACH_MODCMD', name + '_modcmd'))
    binary = path.with_suffix('')
    subprocess.run(['cc', '-std=c99', str(path), '-o', str(binary)], check=True)
    sys.exit(subprocess.run([str(binary)]).returncode)
