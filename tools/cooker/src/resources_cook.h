// F6 (A3, API 9): the RSRC section (Resources.Load names) and the Project.json "additiveScenes" flag. Called from cook.cpp.
#pragma once
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "rensga/core/pack_format.h"

namespace rensga {
namespace cooker {

struct ResourceAsset {
  std::string rel;   // project-relative path with '/' separators, e.g. "Assets/Resources/sfx/jump.wav"
  std::string guid;  // from the .meta
};

// Collects every asset under a folder named "Resources" (any depth under Assets/) into PackResource records sorted by name hash
// (ties in path order). The name is the path below the Resources folder without its extension ("sfx/jump"). `addStr` stores a name in
// STRS and returns its offset. Returns false and fills `error` on a duplicate name of the same kind. Assets of a kind Resources cannot
// load (scenes, scripts, unknown extensions) are skipped.
bool buildResourceTable(const std::vector<ResourceAsset>& assets, const std::function<u32(const std::string&)>& addStr,
                        std::vector<PackResource>& out, std::string& error);

// Sets kSceneAdditive in PackSceneRec::reserved for the scenes whose GUID is listed in Project.json "additiveScenes". Returns the
// first listed GUID that is not a scene of the build ("" when all are), so the caller can report it.
std::string markAdditiveScenes(const nlohmann::json& projectJson, const std::vector<std::string>& sceneGuids, std::vector<PackSceneRec>& scenes);

// ProjectSettings/Project.json "componentReserve": {"<Component type>": <extra slots>} -> PCAP records (empty without the key). A type name
// the engine does not know, or a count that is not a whole number from 1 to 4096, fills `error`.
bool buildPoolReserve(const nlohmann::json& projectJson, std::vector<PackPoolReserve>& out, std::string& error);

}  // namespace cooker
}  // namespace rensga
