// F2.03: desktop hot reload of a cooked pack. The watcher polls the pack file's modification time and
// size; when either changes it reports true once and the runtime reloads the pack (tools/cooker writes
// game.rpak through a temp file + rename, so a changed stamp always means a complete pack).
// Host only: the class does not exist in console builds (PSP, PS2) where the disc/memory card is fixed.
#pragma once
#include <string.h>
#include <sys/stat.h>

#include "rensga/core/types.h"

#if !defined(PSP) && !defined(_EE) && !defined(__PS2__)
#define RENSGA_HAS_PACK_WATCHER 1

namespace rensga {

class PackWatcher {
 public:
  // Starts watching `path` (copied). The current stamp becomes the baseline, so the first poll()
  // after watch() reports false unless the file changed in between.
  void watch(const char* path) {
    strncpy(path_, path, sizeof(path_) - 1);
    path_[sizeof(path_) - 1] = 0;
    calls_ = 0;
    valid_ = stamp(&mtime_, &size_);
  }
  bool watching() const { return path_[0] != 0; }
  // Cheap to call every frame: the file system is only asked every `everyNthCall` calls.
  bool poll(u32 everyNthCall = 30) {
    if (!watching()) return false;
    if (++calls_ < everyNthCall) return false;
    calls_ = 0;
    return check();
  }
  // Checks right now, ignoring the call throttle.
  bool check() {
    i64 m = 0, s = 0;
    bool ok = stamp(&m, &s);
    if (ok == valid_ && m == mtime_ && s == size_) return false;
    valid_ = ok;
    mtime_ = m;
    size_ = s;
    return ok;  // a vanished file is not a reload; the next appearance is
  }

 private:
  bool stamp(i64* m, i64* s) const {
    struct stat st;
    if (stat(path_, &st) != 0) return false;
    *m = static_cast<i64>(st.st_mtime) * 1000000000ll;
#if defined(__linux__)
    *m += st.st_mtim.tv_nsec;
#endif
    *s = static_cast<i64>(st.st_size);
    return true;
  }
  char path_[512] = {};
  i64 mtime_ = 0, size_ = 0;
  u32 calls_ = 0;
  bool valid_ = false;
};

}  // namespace rensga
#endif
