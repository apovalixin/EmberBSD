#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD - exercise threads in a native module loaded by system Lua.
set -eu
umask 077
[ "$(uname -s)" = NetBSD ] || { echo 'Run natively on EmberBSD/NetBSD.' >&2; exit 2; }
[ "$#" -le 1 ] || { echo 'Usage: test-lua-pthread.sh [LUA_EXECUTABLE]' >&2; exit 2; }
interpreter=${1:-/usr/bin/lua}
[ -x "$interpreter" ] || { echo 'Lua executable not found.' >&2; exit 2; }
work=$(mktemp -d /tmp/emberbsd-lua-pthread.XXXXXX)
trap 'rm -rf "$work"' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
cat > "$work/module.c" <<'SRC'
/* SPDX-License-Identifier: BSD-2-Clause */
/* Detect NetBSD libc thread stubs after Lua dlopen. */
#include <pthread.h>
#include <string.h>
#include <lua.h>
#include <lauxlib.h>

static void *
worker(void *arg)
{
	*(int *)arg = 42;
	return NULL;
}

int luaopen_pthread_probe(lua_State *);

int
luaopen_pthread_probe(lua_State *state)
{
	pthread_t thread;
	int status, value = 0;

	status = pthread_create(&thread, NULL, worker, &value);
	if (status != 0)
		return luaL_error(state, "pthread_create: %s", strerror(status));
	status = pthread_join(thread, NULL);
	if (status != 0)
		return luaL_error(state, "pthread_join: %s", strerror(status));
	lua_pushinteger(state, value);
	return 1;
}
SRC
${CC:-cc} -Wall -Wextra -Werror -fPIC -shared "$work/module.c" \
    -llua -lpthread -o "$work/module.so"
cat > "$work/test.lua" <<'LUA'
assert(_VERSION == 'Lua 5.4')
local open = assert(package.loadlib(assert(arg[1]), 'luaopen_pthread_probe'))
assert(open() == 42, 'native thread did not complete')
print('PASS: Lua native module creates and joins a real pthread')
LUA
# A preload would hide a missing dependency on the interpreter executable.
unset LD_PRELOAD
ulimit -c 0
"$interpreter" "$work/test.lua" "$work/module.so"
