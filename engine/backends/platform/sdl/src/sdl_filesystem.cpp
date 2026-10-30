#include "rensga/platform/sdl_filesystem.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>

#include "rensga/core/log.h"

namespace rensga {

SdlFileSystem::SdlFileSystem(const char* dataRoot, const char* org, const char* app, const char* logPath) {
  snprintf(dataRoot_, sizeof(dataRoot_), "%s", dataRoot ? dataRoot : "");
  logPath_[0] = 0;
  if (logPath) snprintf(logPath_, sizeof(logPath_), "%s", logPath);
  savePath_[0] = 0;
  for (void*& s : streams_) s = nullptr;
  char* pref = SDL_GetPrefPath(org, app);  // SDL allocates; freed right here, once, at startup
  if (pref) {
    snprintf(savePath_, sizeof(savePath_), "%s", pref);
    SDL_free(pref);
  }
}

SdlFileSystem::~SdlFileSystem() {
  for (int i = 0; i < kStreamSlots; ++i) closeStream(i + 1);
}

// Maps a virtual path to a real one. Handles are 1-based; 0 and negatives are errors.
bool SdlFileSystem::resolve(const char* path, char* out, u32 outSize, bool& isSave) const {
  isSave = false;
  if (!path) return false;
  const char* rel = path;
  if (!strncmp(path, "save:/", 6)) {
    isSave = true;
    rel = path + 6;
  } else if (!strncmp(path, "data:/", 6)) {
    rel = path + 6;
  } else if (strchr(path, ':')) {
    return false;  // unknown device prefix
  }
  if (strstr(rel, "..")) return false;  // virtual paths stay inside their root
  const char* root = isSave ? savePath_ : dataRoot_;
#if defined(__ANDROID__)
  if (!isSave) root = "";  // APK assets: path relative to the assets folder
#endif
  if (isSave && !root[0]) return false;
  int n = snprintf(out, outSize, "%s%s", root, rel);
  return n > 0 && static_cast<u32>(n) < outSize;
}

Span<const u8> SdlFileSystem::readFile(const char* path, Arena& arena) {
  char full[768];
  bool isSave;
  if (!resolve(path, full, sizeof(full), isSave)) {
    RLOG_ERROR("fs", "bad path %s", path ? path : "(null)");
    return {};
  }
  SDL_RWops* rw = SDL_RWFromFile(full, "rb");
  if (!rw) {
    RLOG_ERROR("fs", "cannot open %s", full);
    return {};
  }
  Sint64 n = SDL_RWsize(rw);
  u8* mem = n >= 0 ? static_cast<u8*>(arena.allocate(static_cast<size_t>(n) + 1)) : nullptr;
  if (!mem) {
    SDL_RWclose(rw);
    RLOG_ERROR("fs", "no arena space for %s", full);
    return {};
  }
  size_t got = SDL_RWread(rw, mem, 1, static_cast<size_t>(n));
  SDL_RWclose(rw);
  if (got != static_cast<size_t>(n)) return {};
  return Span<const u8>{mem, got};
}

void SdlFileSystem::writeLog(const char* text) {
  if (!logPath_[0]) {
    RLOG_INFO("game", "%s", text);
    return;
  }
  FILE* f = fopen(logPath_, "a");
  if (!f) return;
  fputs(text, f);
  fputc('\n', f);
  fclose(f);
}

i32 SdlFileSystem::openStream(const char* path) {
  char full[768];
  bool isSave;
  if (!resolve(path, full, sizeof(full), isSave)) return -1;
  for (int i = 0; i < kStreamSlots; ++i) {
    if (streams_[i]) continue;
    SDL_RWops* rw = SDL_RWFromFile(full, "rb");
    if (!rw) return -1;
    streams_[i] = rw;
    return i + 1;
  }
  RLOG_ERROR("fs", "all %d stream slots are in use", kStreamSlots);
  return -1;
}

u32 SdlFileSystem::readStream(i32 h, void* dst, u32 bytes) {
  if (h < 1 || h > kStreamSlots || !streams_[h - 1]) return 0;
  return static_cast<u32>(SDL_RWread(static_cast<SDL_RWops*>(streams_[h - 1]), dst, 1, bytes));
}

bool SdlFileSystem::seekStream(i32 h, u32 offset) {
  if (h < 1 || h > kStreamSlots || !streams_[h - 1]) return false;
  return SDL_RWseek(static_cast<SDL_RWops*>(streams_[h - 1]), static_cast<Sint64>(offset), RW_SEEK_SET) >= 0;
}

void SdlFileSystem::closeStream(i32 h) {
  if (h < 1 || h > kStreamSlots || !streams_[h - 1]) return;
  SDL_RWclose(static_cast<SDL_RWops*>(streams_[h - 1]));
  streams_[h - 1] = nullptr;
}

bool SdlFileSystem::writeFile(const char* path, const void* data, u32 bytes) {
  char full[768];
  bool isSave;
  if (!resolve(path, full, sizeof(full), isSave) || !isSave) return false;  // only save:/ is writable
  SDL_RWops* rw = SDL_RWFromFile(full, "wb");
  if (!rw) return false;
  size_t put = bytes ? SDL_RWwrite(rw, data, 1, bytes) : 0;
  SDL_RWclose(rw);
  return put == bytes;
}

}  // namespace rensga
