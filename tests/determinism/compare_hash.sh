#!/usr/bin/env bash
# F2.G2: compares the state hash (and, if both logs were made with --trace / trace=1, the per-frame hashes) of two sim logs.
# Usage: compare_hash.sh <log-a> <log-b>    (a = Linux rensga_headless, b = PSP rensga_sim)
# Prints "determinism: MATCH hash=<hex>" or the first divergent frame (and "rerun with --obj-frame N" hint).
set -u
A="$1"; B="$2"
hash() { grep -oE 'rensga: hash [0-9a-f]{8}' "$1" | tail -1 | awk '{print $3}'; }
ha="$(hash "$A")"; hb="$(hash "$B")"
if [ -z "$ha" ] || [ -z "$hb" ]; then echo "determinism: missing hash (a='$ha' b='$hb')" >&2; exit 2; fi
if [ "$ha" = "$hb" ]; then echo "determinism: MATCH hash=$ha"; exit 0; fi
echo "determinism: MISMATCH a=$ha b=$hb"
tr_() { grep -oE 'rensga: trace f=[0-9]+ h=[0-9a-f]{8}' "$1" | sed 's/rensga: trace //'; }
first="$(diff <(tr_ "$A") <(tr_ "$B") | grep -m1 '^[<>]' | sed -E 's/^[<>] f=([0-9]+).*/\1/')"
if [ -n "$first" ]; then echo "first divergent frame: $first (rerun both with --obj-frame $first / objframe=$first and diff the 'rensga: obj' lines)"; fi
objs() { grep -oE 'rensga: obj f=.*' "$1" | sed 's/rensga: obj //'; }
if [ -n "$(objs "$A")" ] && [ -n "$(objs "$B")" ]; then
  echo "first divergent object:"; diff <(objs "$A") <(objs "$B") | grep -m2 '^[<>]'
fi
exit 1
