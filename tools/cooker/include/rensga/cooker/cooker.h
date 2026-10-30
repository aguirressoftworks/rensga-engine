// MVP-20/22: shared cooker core. HOST ONLY. Loads a project (ProjectSettings + Assets) through
// reflection and writes <project>/Library/Cooked/<target>/game.rpak plus a budget report.
// Never names a console: the format comes from the ICookerFormatPlugin registered for the target.
#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "rensga/cooker/plugin.h"
#include "rensga/core/pack_format.h"

namespace rensga {

// ---- ConsoleTarget (ProjectSettings/ConsoleTarget.json) --------------------------------------
struct ConsoleTargetData {
  std::string profile;  // "psp"
  std::string scalar;   // "float" | "fixed"
  uint32_t width = 0, height = 0;
  uint64_t ramBytes = 0, vramBytes = 0, trianglesPerFrame = 0, maxObjects = 0, maxComponentsPerType = 0;
  std::string depth, media;
  uint32_t audioSampleRate = 22050, audioVoices = 16;  // optional "audio": {"sampleRate", "voices"}
  uint32_t maxCoroutines = 64, maxInvokes = 64;  // optional budgets.maxCoroutines / maxInvokes (F3.05)
  bool rumble = false;  // optional "capabilities": {"rumble": bool}: the target's pad has vibration motors (F3.06)
  uint32_t refreshHz = 60;  // optional "refreshHz" (60 NTSC, 50 PAL); the loop's fixed step is 1/refreshHz
  std::vector<std::string> allowedComponents;
  bool sectorAligned = false;  // optional "packSectorAligned"; defaults to true for disc media (iso, disc, cd, dvd)
  int sphereSegments = 12, capsuleSegments = 12;
  // F3.02: texture formats the target accepts ("rgba8", "clut4", "clut8"); empty = RGBA8 only.
  std::vector<std::string> textureFormats;
  // F3.02: optional "framebuffer" {"buffers", "bpp", "depthBits"}; feeds the editor's VRAM map. Defaults: 2 x 32-bit,
  // 16-bit depth when depth == "zbuffer".
  uint32_t fbBuffers = 2, fbBpp = 32, fbDepthBits = 16;
};

// Throws std::runtime_error with a message naming the file and field.
ConsoleTargetData loadConsoleTarget(const std::filesystem::path& file);

// console_target.gen.h: constants plus a profile define (ENGINE_PROFILE_PSP 1).
std::string generateConsoleTargetHeader(const ConsoleTargetData& target);

// ---- primitives (MVP-19) ----------------------------------------------------------------------
enum class PrimitiveShape { Cube, Sphere, Capsule, Plane, Ramp };
struct PrimitiveParams {
  PrimitiveShape shape = PrimitiveShape::Cube;
  Vector3 size = Vector3(1, 1, 1);  // cube, plane (x,z), ramp
  Scalar radius = 0.5f;             // sphere, capsule
  Scalar height = 2.0f;             // capsule (total, caps included)
  Scalar cellSize = 1.0f;           // plane: grid cell edge. PSP GU drops whole triangles that cross the
                                    // near plane or guard band, so large planes must be subdivided.
};
struct PrimitiveCollider {
  bool isSphere = false;
  Vector3 center;        // box
  Vector3 size;          // box
  Scalar radius = 0;     // sphere
};
bool parsePrimitive(const std::string& jsonText, PrimitiveParams& out, std::string& error);
PrimitiveMesh generatePrimitive(const PrimitiveParams& p, const ConsoleTargetData& tessellation);
PrimitiveCollider colliderForPrimitive(const PrimitiveParams& p);

// ---- cook --------------------------------------------------------------------------------------
struct CookMessage {
  enum Severity { Info, Warning, Error } severity = Info;
  std::string text;
  std::string asset;      // project-relative path ("" if none)
  std::string assetGuid;  // GUID text ("" if none)
};
using CookSink = std::function<void(const CookMessage&)>;

struct CookOptions {
  std::filesystem::path project;
  std::string target = "psp";
  bool force = false;                     // ignore the content-hash cache
  std::filesystem::path genHeader;        // if set, console_target.gen.h is written here
};

struct BudgetUsage {
  uint64_t packBytes = 0, triangles = 0, objects = 0, maxComponentsOfOneType = 0, vertexBytes = 0;
  std::string maxComponentsType;
};

struct CookResult {
  bool ok = false;
  bool upToDate = false;  // nothing changed since the last cook
  uint32_t assetsCooked = 0, assetsCached = 0;  // PNG/WAV assets cooked / reused from Library/Cache/<target>
  std::filesystem::path packPath, reportPath;
  BudgetUsage usage;
  std::vector<CookMessage> errors;
};

CookResult cookProject(const CookOptions& options, const CookSink& sink = nullptr);

// ---- budget validator (MVP-22) -----------------------------------------------------------------
// ---- asset cooking (F2.03) ---------------------------------------------------------------------
struct AssetSource {
  std::filesystem::path path;  // absolute
  std::string rel;             // project-relative
  std::string guid;
  std::string importSettings;  // JSON text of the .meta "importSettings" object ("" = defaults)
};
struct CookedAssets {
  std::vector<uint8_t> blob;
  std::vector<PackTexRec> textures;
  std::vector<PackAudioRec> audio;
  std::vector<PackTilemapRec> tilemaps;
  std::vector<PackAnimRec> anims;
  std::vector<std::string> animNames;  // parallel to anims; the caller fills PackAnimRec::nameStr
  std::vector<PackFontRec> fonts;
  uint64_t textureBytes = 0;
};
// Per-asset cook cache (F2.03). Entries are keyed by hash(source bytes + importSettings + salt), where the salt covers the
// cooker version, target, ConsoleTarget and reflected layout, so any of them changing misses the cache. An entry is the
// target plugin's output for one PNG or WAV (the expensive importers); the pack is assembled from it exactly as from a fresh
// cook, so pack bytes do not depend on the cache.
struct AssetCache {
  std::filesystem::path dir;  // Library/Cache/<target>; empty = cache off
  std::string salt;
  bool reuse = true;          // false (--force): never read entries, still write them
  uint32_t cooked = 0, cached = 0;  // out: PNG/WAV assets cooked this run / taken from the cache
  bool prune = true;          // false: leave entries of other assets alone (single-asset reimport in the editor)
};
// Same text as the salt cookProject uses, so an editor reimport and a full cook share Library/Cache/<target>/ entries.
std::string assetCacheSalt(const std::filesystem::path& projectRoot, const std::string& target);
std::string reflectionLayoutHash();
// Cooks every asset with a known extension (.png, .wav, .tilemap.json, .anim.json, .font.json) plus the
// default font, in project-path order. Reports errors (naming the asset and GUID) through `report`
// and returns false if any failed.
bool cookAssets(const std::vector<AssetSource>& sources, ICookerFormatPlugin& plugin, CookedAssets& out,
                const std::function<void(CookMessage::Severity, const std::string& text, const std::string& rel, const std::string& guid)>& report,
                AssetCache* cache = nullptr);

// F2.05: reads ProjectSettings/Physics2D.json into the PH2D matrix. Returns false and fills `error` on a bad file.
// Format: {"format":1, "layers":["Default","Player",...], "ignore":[[a,b],...]}. a and b are layer indices or
// names from "layers". Every pair not listed collides. Optional "matrix": 32 bitmasks, applied first.
bool cookPhysics2D(const std::string& jsonText, PackPhysics2D& out, std::string& error);

struct SceneBudgetInput {
  std::string sceneAsset;  // project-relative path
  std::string sceneGuid;
  uint64_t packBytes = 0;
  uint64_t textureBytes = 0;
  uint64_t vertexBytes = 0;
  uint64_t triangles = 0;
  uint64_t objects = 0;
  struct TypeCount { std::string type; uint64_t count; };
  std::vector<TypeCount> componentsPerType;
  // Heaviest mesh owner, named in the triangle-budget error.
  std::string heaviestObjectName, heaviestObjectGuid;
  uint64_t heaviestTriangles = 0;
};
// Appends one error per exceeded budget; each names the asset and GUID. Returns true if all fit.
bool validateBudgets(const ConsoleTargetData& target, const SceneBudgetInput& in, std::vector<CookMessage>& errors);

}  // namespace rensga
