#!/bin/sh
# Origin: EmberBSD production native console regressions, 2026-10-06.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/virtgpu-console.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
extract() {
    awk -v name="$1" -v type="$3" '
        $0 ~ "^" name "\\(" && $0 !~ /;[[:space:]]*$/ {
            print type; copying = 1;
        }
        copying { print }
        copying && /^}/ { exit }
    ' "$2"
}
console="$src/sys/external/bsd/drm2/virtio/virtgpu_console.c"
cat > "$work/console.c" <<'C'
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include "virtgpu_limits.h"
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define READ_ONCE(x) (x)
#define membar_producer() ((void)0)
#define membar_consumer() ((void)0)
#define msecs_to_jiffies(x) (x)
#define WSDISPLAYIO_MODE_EMUL 0
struct mutex { bool held; };
static void mutex_lock(struct mutex *m) { assert(!m->held); m->held=true; }
static void mutex_unlock(struct mutex *m) { assert(m->held); m->held=false; }
#define spin_lock mutex_lock
#define spin_unlock mutex_unlock
static unsigned atomic_swap_uint(volatile unsigned *p, unsigned x) {
    unsigned old=*p; *p=x; return old;
}
struct work_struct { int unused; };
struct delayed_work { struct work_struct work; };
struct drm_framebuffer { void *obj[1]; };
struct drm_device { void *dev_private; };
struct drm_file { int unused; };
struct drm_fb_helper { struct drm_framebuffer *fb; };
struct drmfb_softc { struct { struct drm_fb_helper *da_fb_helper; } sc_da; };
struct virtgpu_console;
struct virtio_gpu_device {
    struct virtgpu_console *console; bool vqs_ready; int submit_error;
};
struct virtgpu_console {
    struct drm_fb_helper helper;
    struct virtio_gpu_device *vgdev;
    struct mutex lock, schedule_lock;
    void *wq;
    struct delayed_work work;
    void *shadow;
    size_t size;
    volatile unsigned dirty;
    bool emul, master, restore, stopped;
};
struct virtio_gpu_object {
    void *dma_vaddr; unsigned width, height, hw_res_handle;
    struct { struct { int unused; } base; } base;
};
#define gem_to_virtio_gpu_obj(x) ((struct virtio_gpu_object *)(x))
struct virtio_gpu_object_array { int unused; };
static struct virtio_gpu_object_array array;
static struct virtgpu_console *current;
static unsigned restores, transfers, flushes, queued, stops, cancels;
static bool inject_damage, fail_alloc;
static int transfer_error;
static void virtgpu_console_stop(struct virtio_gpu_device *);
static void virtio_gpu_stop(struct virtio_gpu_device *v, int error) {
    assert(!v->console->lock.held); stops++; v->submit_error=error;
    v->vqs_ready=false; virtgpu_console_stop(v);
}
static int drm_fb_helper_restore_fbdev_mode_unlocked(struct drm_fb_helper *h) {
    assert(h == &current->helper && current->lock.held);
    restores++; return 0;
}
static void queue_delayed_work(void *q, struct delayed_work *w, int delay) {
    (void)q; (void)w; (void)delay;
    assert(current->schedule_lock.held && !current->stopped); queued++;
}
static void cancel_delayed_work_sync(struct delayed_work *w) {
    assert(w == &current->work && current->stopped); cancels++;
}
static struct virtio_gpu_object_array *virtio_gpu_array_alloc(int n) {
    assert(n == 1); return fail_alloc ? NULL : &array;
}
static void virtio_gpu_array_add_obj(struct virtio_gpu_object_array *a, void *o) {
    assert(a == &array && o != NULL);
}
static int virtio_gpu_cmd_transfer_to_host_2d(struct virtio_gpu_device *v,
    uint64_t off, uint32_t w, uint32_t h, uint32_t x, uint32_t y,
    struct virtio_gpu_object_array *a, void *f) {
    assert(v == current->vgdev && current->lock.held);
    assert(!current->master && current->emul && a == &array && !f);
    assert(off == 0 && x == 0 && y == 0 && w == 32 && h == 32);
    transfers++;
    if (inject_damage) {
        struct virtio_gpu_object *bo=current->helper.fb->obj[0];
        unsigned char before=((unsigned char *)bo->dma_vaddr)[0];
        memset(current->shadow, 0x7b, current->size);
        atomic_swap_uint(&current->dirty, 1);
        assert(((unsigned char *)bo->dma_vaddr)[0] == before);
        inject_damage=false;
    }
    return transfer_error;
}
static int virtio_gpu_cmd_resource_flush(struct virtio_gpu_device *v,
    unsigned id, unsigned x, unsigned y, unsigned w, unsigned h) {
    assert(v == current->vgdev && id == 1 && !x && !y && w==32 && h==32);
    flushes++; return 0;
}
C
for pair in 'virtgpu_console_geometry size_t' 'virtgpu_console_damage void' \
 'virtgpu_console_mode bool' 'virtgpu_console_upload int' \
 'virtgpu_console_work void' 'virtgpu_console_master_set int' \
 'virtgpu_console_master_drop void' 'virtgpu_console_stop void' \
 'virtgpu_console_drain void'; do
    set -- $pair
    extract "$1" "$console" "static $2" >> "$work/console.c"
done
cat >> "$work/console.c" <<'C'
int main(void) {
    unsigned char shadow[4096], dma[4096];
    struct virtio_gpu_object bo={ .dma_vaddr=dma, .width=32, .height=32,
        .hw_res_handle=1 };
    struct drm_framebuffer fb={ .obj={ &bo } };
    struct virtio_gpu_device v={ .vqs_ready=true };
    struct virtgpu_console vc={ .helper={ &fb }, .vgdev=&v,
        .shadow=shadow, .size=sizeof(shadow), .emul=true };
    struct drm_device dev={ &v };
    struct drmfb_softc sc={ .sc_da={ &vc.helper } };
    current=&vc; v.console=&vc;
    assert(!virtgpu_console_geometry(0,32));
    assert(!virtgpu_console_geometry(32,31));
    assert(!virtgpu_console_geometry(UINT32_MAX,8192));
    assert(!virtgpu_console_geometry(16384,32));
    assert(!virtgpu_console_geometry(32,8193));
    assert(virtgpu_console_geometry(800,600)==1920000);
    memset(shadow,0x11,sizeof(shadow));
    virtgpu_console_damage(&sc); inject_damage=true;
    virtgpu_console_work(&vc.work.work);
    assert(transfers==1 && flushes==1 && vc.dirty==1 && dma[0]==0x11);
    virtgpu_console_work(&vc.work.work);
    assert(transfers==2 && flushes==2 && vc.dirty==0 && dma[0]==0x7b);
    virtgpu_console_work(&vc.work.work); assert(transfers==2);
    assert(!virtgpu_console_master_set(&dev,NULL,true));
    virtgpu_console_damage(&sc); vc.restore=true;
    virtgpu_console_work(&vc.work.work);
    assert(restores==0 && transfers==2 && vc.dirty);
    virtgpu_console_mode(&sc,1); virtgpu_console_master_drop(&dev,NULL);
    virtgpu_console_work(&vc.work.work); assert(restores==0 && transfers==2);
    virtgpu_console_mode(&sc,0); virtgpu_console_work(&vc.work.work);
    assert(restores==1 && transfers==3 && flushes==3);
    virtgpu_console_damage(&sc); transfer_error=-EIO;
    unsigned q=queued; virtgpu_console_work(&vc.work.work);
    assert(stops==1 && vc.stopped && !v.vqs_ready && queued==q);
    virtgpu_console_damage(&sc); virtgpu_console_work(&vc.work.work);
    assert(transfers==4 && queued==q);
    virtgpu_console_drain(&v); assert(cancels==1);
    v.vqs_ready=true; v.submit_error=0; vc.stopped=false;
    transfer_error=0; fail_alloc=true;
    virtgpu_console_damage(&sc); virtgpu_console_work(&vc.work.work);
    assert(stops==2 && vc.stopped && transfers==4 && flushes==3);
    puts("VirtGPU production console geometry, damage/upload, ownership and stop contracts passed");
}
C
${CC:-cc} -std=c99 -Wall -Wextra -Werror -Wno-unused-parameter \
 -I"$src/sys/external/bsd/drm2/virtio" "$work/console.c" -o "$work/console"
"$work/console"
cat > "$work/selection.c" <<'C'
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#define KASSERT assert
typedef void *device_t;
struct simplefb_softc { int unused; };
static struct simplefb_softc *simplefb_deferred;
static device_t simplefb_owner;
static bool simplefb_committed;
static unsigned attaches;
static void *device_private(void *p) { return p; }
static int simplefb_attach_genfb(struct simplefb_softc *sc) {
    assert(sc); attaches++; return 0;
}
C
simplefb="$src/sys/dev/fdt/simplefb.c"
extract simplefb_console_reserve "$simplefb" 'static bool' >> "$work/selection.c"
extract simplefb_console_commit "$simplefb" 'static void' >> "$work/selection.c"
extract simplefb_console_fallback "$simplefb" 'static int' >> "$work/selection.c"
cat >> "$work/selection.c" <<'C'
int main(void) {
    struct simplefb_softc sc;
    int gpu, other;
    assert(!simplefb_console_reserve(&gpu));
    simplefb_deferred=&sc;
    assert(simplefb_console_reserve(&gpu));
    assert(!simplefb_console_reserve(&other));
    simplefb_console_fallback(&sc); assert(attaches==1);
    simplefb_console_fallback(&sc); assert(attaches==1);
    simplefb_owner=NULL; simplefb_deferred=&sc;
    assert(simplefb_console_reserve(&gpu));
    simplefb_console_commit(&gpu);
    simplefb_console_fallback(&sc); assert(attaches==1);
    assert(!simplefb_console_reserve(&other));
    simplefb_committed=false; simplefb_owner=NULL; simplefb_deferred=&sc;
    simplefb_console_fallback(&sc); assert(attaches==2);
    puts("VirtGPU production early selection, single final console and failure fallback passed");
}
C
${CC:-cc} -std=c99 -Wall -Wextra -Werror "$work/selection.c" -o "$work/selection"
"$work/selection"
