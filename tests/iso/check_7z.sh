#!/bin/sh
# Builds a PS2-style image with rensga-iso and checks that 7z lists the same files with the same sizes.
set -eu
ISO_BIN="${1:?rensga-iso}"
command -v 7z >/dev/null 2>&1 || { echo "7z not installed"; exit 77; }
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
mkdir "$T/in" "$T/in/SUB"
printf 'BOOT2 = cdrom0:\\RENSGA.ELF;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n' > "$T/in/SYSTEM.CNF"
head -c 70001 /dev/zero | tr '\0' 'E' > "$T/in/RENSGA.ELF"
head -c 5000 /dev/zero | tr '\0' 'R' > "$T/in/GAME.RPK"
head -c 3 /dev/zero > "$T/in/SUB/A.BIN"
"$ISO_BIN" -o "$T/a.iso" -V RENSGA --order SYSTEM.CNF,RENSGA.ELF,GAME.RPK "$T/in" >/dev/null
7z l "$T/a.iso" > "$T/list.txt"
for f in SYSTEM.CNF:56 RENSGA.ELF:70001 GAME.RPK:5000 SUB/A.BIN:3; do
  n="${f%%:*}"; s="${f##*:}"
  grep -E "[[:space:]]$s[[:space:]].*$n\$" "$T/list.txt" >/dev/null || { echo "7z does not list $n with size $s"; cat "$T/list.txt"; exit 1; }
done
7z t "$T/a.iso" >/dev/null
echo ok
