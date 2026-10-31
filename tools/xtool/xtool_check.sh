#!/usr/bin/env bash
# Test for rensga-xtool, run inside a private Xvfb (never on the desktop). It compiles a tiny X client with a _NET_WM_PID,
# a child window and a key handler, then checks wait-window, find-pid, tree (geometry, nesting) and key injection (XTest).
# Usage: xtool_check.sh <rensga-xtool>   Exit 77 = skipped (no xvfb-run).
set -u
XT="${1:?rensga-xtool path}"; shift || true
command -v xvfb-run >/dev/null || { echo "skip: no xvfb-run"; exit 77; }
if [ -z "${RENSGA_XTOOL_INNER:-}" ]; then
  # Re-exec inside Xvfb, never on the desktop.
  exec env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 RENSGA_XTOOL_INNER=1 xvfb-run -a "$0" "$XT" "$@"
fi
fail() { echo "xtool_check: FAIL: $*" >&2; exit 1; }

# 1. root tree
out=$("$XT" tree root) || fail "tree root"
echo "$out" | head -1 | grep -qE '^0x[0-9a-f]+ [0-9-]+ [0-9-]+ [0-9]+ [0-9]+' || fail "tree root format: $out"

# 2. usage / unknown key
"$XT" key NoSuchKeysymXYZ tap >/dev/null 2>&1 && fail "unknown keysym should fail"
"$XT" bogus >/dev/null 2>&1; [ $? -eq 2 ] || fail "usage exit code"

# 3. a client window with _NET_WM_PID: the helper client below is compiled on the fly (cc + libX11 are present wherever xtool built).
tmp=$(mktemp -d); trap 'kill $cpid 2>/dev/null; rm -rf "$tmp"' EXIT
cat > "$tmp/client.c" <<'C'
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(int argc, char** argv) {
  Display* d = XOpenDisplay(NULL);
  if (!d) return 2;
  Window w = XCreateSimpleWindow(d, DefaultRootWindow(d), 10, 20, 200, 100, 0, 0, 0);
  Window child = XCreateSimpleWindow(d, w, 5, 6, 50, 40, 0, 0, 0);
  unsigned long pid = (unsigned long)getpid();
  XChangeProperty(d, w, XInternAtom(d, "_NET_WM_PID", False), XA_CARDINAL, 32, PropModeReplace, (unsigned char*)&pid, 1);
  XSelectInput(d, w, KeyPressMask);
  XMapWindow(d, w);
  XMapWindow(d, child);
  XSetInputFocus(d, w, RevertToParent, CurrentTime);
  XFlush(d);
  FILE* f = fopen(argv[1], "w"); fprintf(f, "ready\n"); fclose(f);
  for (int i = 0; i < 300; ++i) {
    while (XPending(d)) {
      XEvent e; XNextEvent(d, &e);
      if (e.type == KeyPress && XLookupKeysym(&e.xkey, 0) == XK_a) { f = fopen(argv[2], "w"); fprintf(f, "got a\n"); fclose(f); return 0; }
    }
    usleep(50000);
  }
  return 1;
}
C
cc -o "$tmp/client" "$tmp/client.c" $(pkg-config --cflags --libs x11) || fail "compile client"
"$tmp/client" "$tmp/ready" "$tmp/key" & cpid=$!
"$XT" wait-window "$cpid" 10 >"$tmp/win" || fail "wait-window"
wid=$(head -1 "$tmp/win")
"$XT" find-pid "$cpid" | grep -q "$wid" || fail "find-pid does not list $wid"
tree=$("$XT" tree "$wid")
echo "$tree" | head -1 | grep -qE "^$wid 10 20 200 100" || fail "tree geometry: $tree"
echo "$tree" | sed -n 2p | grep -qE '^  0x[0-9a-f]+ 5 6 50 40' || fail "tree child: $tree"
"$XT" wait-window 1 1 >/dev/null 2>&1 && fail "wait-window for pid 1 should time out"
# 4. key injection reaches the focused client
for i in 1 2 3 4 5; do [ -f "$tmp/ready" ] && break; sleep 0.2; done
"$XT" focus "$wid" || fail "focus"
"$XT" key a tap 40 || fail "key a"
for i in 1 2 3 4 5 6 7 8 9 10; do [ -f "$tmp/key" ] && break; sleep 0.2; done
[ -f "$tmp/key" ] || fail "the client never saw the injected key"
echo "xtool_check: ok"
