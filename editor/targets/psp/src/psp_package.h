// EBOOT.PBP package data (F5.03 / F5.04 / F5.05): title, disc ID and version from the project, package art (ICON0, PIC1, SND0),
// streamed audio and license files. Pure functions on files; no toolchain is started here (psp_target.cpp runs mksfoex and pack-pbp).
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace rensga {
namespace editor {

struct PackageSettings {
  std::string title;      // PARAM.SFO TITLE (at most 127 bytes)
  std::string discId;     // PARAM.SFO DISC_ID: four capital letters and five digits, like RNSG00001
  std::string version;    // "1.00": PARAM.SFO DISC_VERSION; APP_VER is the same as "01.00"
  std::string appVer;     // "01.00"
  std::filesystem::path icon0, pic1, snd0;  // files the project provides ("" = none)
};

// ProjectSettings/Package/Package.json {"format":1,"title","discId","version"} over Project.json ("name", "version"). Art files:
// ProjectSettings/Package/{ICON0.png,PIC1.png,SND0.AT3}, then Assets/Package/ with the same names. Missing or invalid values get
// the defaults (title = project name, disc ID = deriveDiscId(name), version 1.00); `warnings` says what was replaced.
PackageSettings loadPackageSettings(const std::filesystem::path& project, std::vector<std::string>* warnings = nullptr);

std::string deriveDiscId(const std::string& name);   // "RNSG" + five digits from a hash of the name; stable
bool validDiscId(const std::string& id);              // ^[A-Z]{4}[0-9]{5}$
bool normalizeVersion(const std::string& in, std::string& version, std::string& appVer);  // "1.5" -> "1.50" / "01.50"; false = invalid
std::string clampSfoTitle(const std::string& title);  // trimmed, control characters dropped, cut at 127 bytes on a UTF-8 boundary

// Streamed audio next to the pack: Library/Cooked/<target>/*.wav, then <project>/Streaming/*.wav for names not cooked.
std::vector<std::filesystem::path> streamedAudio(const std::filesystem::path& project, const std::filesystem::path& cookedPack);

// Writes <dest>/ with the engine's MIT license, the third-party notices the game binary carries (SDL2 zlib, stb, the SDK) and
// README-LICENSES.txt. Copies the SDK's own license files from `moduleRoot` when it is given. Returns the files written.
std::vector<std::filesystem::path> writeLicenseFiles(const std::filesystem::path& dest, const std::filesystem::path& repoRoot,
                                                     const std::filesystem::path& moduleRoot, std::string& error);

}  // namespace editor
}  // namespace rensga
