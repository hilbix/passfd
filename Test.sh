#!/bin/bash

STDOUT() { local e=$?; printf '%q' "$1"; printf ' %q' "${@:2}"; printf '\n'; return $e; }
STDERR() { local e=$?; STDOUT "$@" >&2; return $e; }
OOPS() { STDERR Test fail: "$@"; exit 23; }
o() { "$@" || OOPS rc=$?: "$@"; STDOUT ok: "$@"; }

S=.tmp/test.sock

o ./passfd v l i "$S" 0 <<< 'hello world' -- ./passfd v o "$S" 7 -- cmp <(echo hello world) /proc/self/fd/7
[ -e "$S" ] && OOPS socket still exists: "$S"

:

