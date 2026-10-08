#!/bin/sh
# Origin: EmberBSD - return an unconfirmed A133 boot through its watchdog.
# SPDX-License-Identifier: BSD-2-Clause

# Source from a root-owned A133 installation hook. The boot loader must already
# have armed signed USB recovery for the next reset. Confirmation removes the
# root-owned marker before the delay expires; normal boots have no marker.
# This helper does not arm recovery, reboot or alter persistent storage.
a133_firstboot_wait()
{
    sleep "$1"
}

a133_firstboot_stop()
{
    /etc/rc.d/mcu7502_keepalive stop
}

a133_firstboot_guard()
{
    [ "$#" -eq 2 ] || return 2
    case "$2" in
        ''|*[!0-9]*|????*) return 2 ;;
    esac
    [ "$2" -ge 1 ] && [ "$2" -le 600 ] || return 2
    [ -f "$1" ] && [ ! -L "$1" ] || return 0
    a133_firstboot_wait "$2" || return 1
    [ -f "$1" ] && [ ! -L "$1" ] || return 0
    a133_firstboot_stop
}
