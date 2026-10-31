/* rensga-xtool: a tiny X11 + XTest helper for the editor and emulator tests, used instead of xdotool / xwininfo (not
 * installed on every machine). Run it only inside a private display: xvfb-run -a rensga-xtool ...
 *
 *   rensga-xtool key <keysym> [down|up|tap] [ms]   send a key through XTest (default tap; ms = hold time of a tap, default 50)
 *   rensga-xtool find-pid <pid>                    print the ids (0x...) of the windows whose _NET_WM_PID is <pid>, one per line
 *   rensga-xtool tree <window>                     print the window and its children recursively: id x y w h (indented)
 *   rensga-xtool wait-window <pid> <timeout_s>     poll until <pid> owns a window; print the first id and exit 0, else exit 1
 *   rensga-xtool focus <window>                    give the window the keyboard focus (a private Xvfb has no window manager to do it)
 *
 * Window ids are hex (0x...) or decimal; `root` means the root window. Exit codes: 0 ok, 1 not found / timeout, 2 usage or no display.
 */
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void sleep_ms(long ms) {
  struct timespec ts;
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (ms % 1000) * 1000000L;
  nanosleep(&ts, NULL);
}

static int window_pid(Display* d, Window w, Atom prop) {
  Atom type;
  int fmt;
  unsigned long n, after;
  unsigned char* data = NULL;
  int pid = -1;
  if (XGetWindowProperty(d, w, prop, 0, 1, False, XA_CARDINAL, &type, &fmt, &n, &after, &data) == Success && data) {
    if (type == XA_CARDINAL && fmt == 32 && n == 1) pid = (int)*(unsigned long*)data;
    XFree(data);
  }
  return pid;
}

/* Depth-first walk; calls visit for every window. */
typedef int (*visit_fn)(Display*, Window, int depth, void*);
static int walk(Display* d, Window w, int depth, visit_fn visit, void* user) {
  if (visit(d, w, depth, user)) return 1;
  Window root, parent, *kids = NULL;
  unsigned int n = 0;
  if (!XQueryTree(d, w, &root, &parent, &kids, &n)) return 0;
  int stop = 0;
  for (unsigned int i = 0; i < n && !stop; ++i) stop = walk(d, kids[i], depth + 1, visit, user);
  if (kids) XFree(kids);
  return stop;
}

struct pid_search {
  int pid;
  Atom prop;
  int found;
  int stop_at_first;
};
static int visit_pid(Display* d, Window w, int depth, void* u) {
  struct pid_search* s = (struct pid_search*)u;
  (void)depth;
  if (window_pid(d, w, s->prop) == s->pid) {
    printf("0x%lx\n", (unsigned long)w);
    s->found++;
    return s->stop_at_first;
  }
  return 0;
}

static int visit_print(Display* d, Window w, int depth, void* u) {
  XWindowAttributes a;
  (void)u;
  if (!XGetWindowAttributes(d, w, &a)) return 0;
  printf("%*s0x%lx %d %d %d %d%s\n", depth * 2, "", (unsigned long)w, a.x, a.y, a.width, a.height,
         a.map_state == IsViewable ? "" : " (unmapped)");
  return 0;
}

static Window parse_window(Display* d, const char* s) {
  if (strcmp(s, "root") == 0) return DefaultRootWindow(d);
  return (Window)strtoul(s, NULL, 0);
}

static int cmd_find(Display* d, int pid, int first) {
  struct pid_search s;
  s.pid = pid;
  s.prop = XInternAtom(d, "_NET_WM_PID", False);
  s.found = 0;
  s.stop_at_first = first;
  walk(d, DefaultRootWindow(d), 0, visit_pid, &s);
  return s.found ? 0 : 1;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: rensga-xtool key|find-pid|tree|wait-window|focus ...\n");
    return 2;
  }
  Display* d = XOpenDisplay(NULL);
  if (!d) {
    fprintf(stderr, "rensga-xtool: cannot open the X display (DISPLAY=%s)\n", getenv("DISPLAY") ? getenv("DISPLAY") : "");
    return 2;
  }
  const char* cmd = argv[1];
  int rc = 2;
  if (strcmp(cmd, "key") == 0 && argc >= 3) {
    KeySym ks = XStringToKeysym(argv[2]);
    if (ks == NoSymbol) {
      fprintf(stderr, "rensga-xtool: unknown keysym '%s'\n", argv[2]);
    } else {
      int ev_base, err_base, maj, min;
      if (!XTestQueryExtension(d, &ev_base, &err_base, &maj, &min)) {
        fprintf(stderr, "rensga-xtool: the XTEST extension is not available\n");
      } else {
        KeyCode kc = XKeysymToKeycode(d, ks);
        const char* mode = argc >= 4 ? argv[3] : "tap";
        long ms = argc >= 5 ? atol(argv[4]) : 50;
        if (!kc) {
          fprintf(stderr, "rensga-xtool: keysym '%s' has no keycode on this display\n", argv[2]);
        } else if (strcmp(mode, "down") == 0) {
          XTestFakeKeyEvent(d, kc, True, 0);
          XFlush(d);
          rc = 0;
        } else if (strcmp(mode, "up") == 0) {
          XTestFakeKeyEvent(d, kc, False, 0);
          XFlush(d);
          rc = 0;
        } else if (strcmp(mode, "tap") == 0) {
          XTestFakeKeyEvent(d, kc, True, 0);
          XFlush(d);
          sleep_ms(ms);
          XTestFakeKeyEvent(d, kc, False, 0);
          XFlush(d);
          rc = 0;
        } else {
          fprintf(stderr, "rensga-xtool: mode must be down, up or tap\n");
        }
        XSync(d, False);
      }
    }
  } else if (strcmp(cmd, "find-pid") == 0 && argc >= 3) {
    rc = cmd_find(d, atoi(argv[2]), 0);
  } else if (strcmp(cmd, "tree") == 0 && argc >= 3) {
    Window w = parse_window(d, argv[2]);
    XWindowAttributes a;
    if (!XGetWindowAttributes(d, w, &a)) {
      fprintf(stderr, "rensga-xtool: no such window %s\n", argv[2]);
      rc = 1;
    } else {
      walk(d, w, 0, visit_print, NULL);
      rc = 0;
    }
  } else if (strcmp(cmd, "focus") == 0 && argc >= 3) {
    Window w = parse_window(d, argv[2]);
    XSetInputFocus(d, w, RevertToParent, CurrentTime);
    XSync(d, False);
    rc = 0;
  } else if (strcmp(cmd, "wait-window") == 0 && argc >= 4) {
    int pid = atoi(argv[2]);
    double timeout = atof(argv[3]);
    long waited = 0;
    rc = 1;
    while (waited <= (long)(timeout * 1000.0)) {
      if (cmd_find(d, pid, 1) == 0) {
        rc = 0;
        break;
      }
      sleep_ms(100);
      waited += 100;
    }
    if (rc) fprintf(stderr, "rensga-xtool: no window for pid %d after %.1f s\n", pid, timeout);
  } else {
    fprintf(stderr, "usage: rensga-xtool key <keysym> [down|up|tap] [ms] | find-pid <pid> | tree <window> | wait-window <pid> <timeout_s> | focus <window>\n");
  }
  XCloseDisplay(d);
  return rc;
}
