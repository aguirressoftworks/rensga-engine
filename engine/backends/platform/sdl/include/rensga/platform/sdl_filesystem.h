// SDL2 file system (PC + Android), IFileSystem contract (Engine 1):
//   "data:/<rel>"  game data root. PC: <dataRoot>/<rel>. Android: APK assets, read through SDL_RWFromFile
//                  (dataRoot is ignored there). A path with no prefix is treated as "data:/<path>".
//   "save:/<rel>"  writable folder from SDL_GetPrefPath(org, app). Parent folders are not created.
// Streams use a fixed slot table (8 slots, no heap). readFile allocates only from the caller's arena.
#pragma once
#include "rensga/core/backends.h"

namespace rensga {

class SdlFileSystem : public IFileSystem {
 public:
  static constexpr int kStreamSlots = 8;
  SdlFileSystem(const char* dataRoot = "", const char* org = "rensga", const char* app = "game", const char* logPath = nullptr);
  ~SdlFileSystem();

  Span<const u8> readFile(const char* path, Arena& arena) override;
  void writeLog(const char* text) override;
  // IFileSystem stream/save methods (contract 3). Declared without `override` so this header also
  // compiles against an interface that predates them; the signatures must match the interface.
  i32 openStream(const char* path) override;
  u32 readStream(i32 h, void* dst, u32 bytes) override;
  bool seekStream(i32 h, u32 offset) override;
  void closeStream(i32 h) override;
  bool writeFile(const char* path, const void* data, u32 bytes) override;

 private:
  bool resolve(const char* path, char* out, u32 outSize, bool& isSave) const;
  char dataRoot_[256];
  char savePath_[512];
  char logPath_[256];
  void* streams_[kStreamSlots];  // SDL_RWops*
};

}  // namespace rensga
