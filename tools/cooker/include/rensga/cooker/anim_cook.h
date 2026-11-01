// F6.03 (B1): property clips (.clip.json) and animator controllers (.controller.json) -> PCLP / ANIC records.
// The formats are documented in docs/rensga-engine/animation.md. The editor bakes a clip in memory with the same function
// (bakePropClip) for the Animation window preview, so what the window shows is what the runtime plays.
#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "rensga/core/pack_format.h"

namespace rensga {
namespace cooker {

struct AnimIssue {
  std::string message, asset, guid;
};

using AddStringFn = std::function<u32(const std::string&)>;

// Appends the clip's curves, keys and events to `blob` (16-byte aligned, offsets are from the start of `blob`) and fills `out`.
// Property names are resolved through TypeRegistry (engine and script components) and the pseudo components Transform and
// GameObject. Returns false with `err` (naming the curve) on a bad clip.
bool bakePropClip(const nlohmann::json& doc, const Guid& guid, const AddStringFn& addStr, std::vector<u8>& blob, PackPropClip& out, std::string& err);

// Where a controller state's motion lives: a property clip of the project, or a skeletal clip (CLP3) known to the glTF import.
struct MotionResolver {
  std::function<bool(const std::string& guidText, PackAnimStateKind& kind, Guid& out, std::string& err)> resolve;
};
// `fixedHz` = fixed steps per second (cross-fade durations are stored in ticks).
bool bakeController(const nlohmann::json& doc, const Guid& guid, u32 fixedHz, const MotionResolver& motions, const AddStringFn& addStr,
                    std::vector<u8>& blob, PackAnimController& out, std::string& err);

struct AnimAssetRef {
  std::filesystem::path path;  // absolute
  std::string rel;
  std::string guid;
};
struct AnimCooked {
  std::vector<PackPropClip> clips;
  std::vector<PackAnimController> controllers;
};
// Cooks every clip and controller. `skeletal` maps a GUID text to a CLP3 guid (a clip of an imported glTF, or the model's first clip);
// return false when it is none. Issues are appended (errors only; a clean cook leaves `issues` empty).
bool cookAnimationAssets(const std::vector<AnimAssetRef>& clipAssets, const std::vector<AnimAssetRef>& controllerAssets, u32 fixedHz,
                         const std::function<bool(const std::string&, Guid&)>& skeletal, const AddStringFn& addStr, std::vector<u8>& blob,
                         AnimCooked& out, std::vector<AnimIssue>& issues);

}  // namespace cooker
}  // namespace rensga
