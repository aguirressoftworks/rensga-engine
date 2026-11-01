// F6.05 (Editor 11): the build targets of Build Settings. The table is data (tools/pipeline/data/platforms.json): a console image of the
// project's profile (built by a TargetModule of editor/targets or by a CMake preset in a build support module) and the PC / phone players.
// Nothing in this library names a console.
#pragma once
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "rensga/pipeline/emulator_launcher.h"
#include "rensga/pipeline/pipeline.h"

namespace rensga {
namespace pipeline {

struct PlatformInfo {
  std::string id;            // the id Build Settings, the CLI and Project.json use; also the cook target
  std::string name;          // shown in Build Settings
  bool console = false;      // an image of the project's own ConsoleTarget profile (profile == id)
  std::string flow;          // "module" (a TargetModule does it), "cmake" (preset + build here), "gradle" (tools/android)
  std::string moduleId;      // build support module ("" = the host's compiler)
  std::string dockerWrapper; // tools/docker/<name>.sh, the contributor fallback ("" = none)
  std::string output;        // what Build produces, for the UI
  std::string devStream;     // "cmake-log" | "env-tcp" | "env-log" | "none": how a development build streams profiler frames
  // flow == cmake
  std::string preset, target, artifact;  // artifact: path of the built player below the CMake tree (gradle: below the repo)
  std::vector<std::string> cmakeDefaults, cmakeArgs;
  struct { std::string native, docker; } copyRuntime;  // shell words copied next to the exe after the build
  struct Package {
    std::string kind;     // "folder" | "script" | "file"
    std::string script, file, exeSuffix, dll, suffix, note;
  } package;
  struct Run {
    std::string kind;     // "process" | "wine" | "script" | "adb"
    std::string script, name, needs, packageId, log;
    int frames = 120, timeoutSeconds = 120;
  } run;
  bool moduleFlow() const { return flow == "module"; }
};
const std::vector<PlatformInfo>& platformInfos();
const PlatformInfo* findPlatform(const std::string& id);
// The platforms Build Settings lists for a project: its console profile (ConsoleTarget.json "profile") first, then the non-console ones.
// One project = one profile, so no other console is offered.
std::vector<std::string> platformsForProject(const std::filesystem::path& project);
// Cooker target id for a platform: the platform id (consoles cook their own profile, the others the PC formats).
std::string cookTargetFor(const std::string& platform);
// A file-name-safe project name from Project.json "name" (falls back to the folder name).
std::string projectBuildName(const std::filesystem::path& project);
// Where a platform's finished build lands: <outputRoot or <project>/Builds>/<platform>.
std::filesystem::path platformOutputDir(const std::filesystem::path& project, const std::string& platform, const std::filesystem::path& outputRoot);

struct PlatformContext {
  std::filesystem::path project;
  std::string platform;
  bool development = false;
  std::filesystem::path cookedPack;   // Library/Cooked/<cook target>/game.rpak
  std::filesystem::path buildDir;     // Library/Build/<platform>-<dev|release>
  std::filesystem::path outDir;       // finished build (platformOutputDir)
  std::string name;                   // projectBuildName
  const std::atomic<bool>* cancel = nullptr;
};

// Compile the player for the platform (reflection codegen, CMake configure + build in buildDir/cmake). `exe` = the player binary / ELF.
bool platformCompile(const PlatformContext& c, const EventSink& sink, std::filesystem::path& exe);
// Package into outDir (folder with exe + pack + streamed music, an image made by a script, or the copied artifact).
// `primary` = the file or folder a user opens.
bool platformPackage(const PlatformContext& c, const std::filesystem::path& exe, const EventSink& sink, std::filesystem::path& primary);

enum class LaunchKind { Started, NotRun, Failed };
// Creates the launcher for a built platform (the exe, wine + exe, an emulator script, adb) and starts it. NotRun: nothing could run
// here (no attached device, no wine): `reason` says why; the build still counts as ok.
LaunchKind platformLaunch(const PlatformContext& c, const std::filesystem::path& primary, const LaunchSpec& spec, const EventSink& sink,
                          std::unique_ptr<EmulatorLauncher>& launcher, std::string& reason);

// Copies a finished module-flow release build (image + pack + music + LICENSES) into outDir.
bool collectFolder(const std::filesystem::path& from, const std::filesystem::path& to, const EventSink& sink);

}  // namespace pipeline
}  // namespace rensga
