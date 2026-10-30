// F2.05 Engine 5: gameplay framework. 2D components, tilemap collision, sprite animation, minimal UI.
//
// Storage: every component here lives in the World's per-type fixed-capacity contiguous pool
// (ComponentPool, sized from scene data at load, no heap per frame). forEach<T> walks the pool in slot order.
//
// Coordinates: 2D components work in VIRTUAL PIXELS (the renderer's virtual resolution), origin top-left, +Y down.
// A 2D object's Transform position is the top-left corner of its sprite or box, minus the pivot/offset fields.
// Rotation is ignored; the x/y lossy scale scales sprite and tile size.
//
// Types here are reflected (components.h rules apply): the inspector, cooker and loader read the one description.
// They are drawn by renderGameplay2D (called by renderWorld after the 3D pass) and stepped by Gameplay2D::
// beginFrame / fixedStep (called by GameLoop).
#pragma once
#include "rensga/core/backends.h"
#include "rensga/core/object.h"
#include "rensga/core/pack_format.h"

namespace rensga {

class World;

// Texture handles of the loaded pack. The runtime main uploads every TEXR record and binds the handles here, so
// components can resolve a texture GUID to a renderer handle without knowing the backend.
struct GfxAssets {
  static constexpr u32 kMaxTextures = 64;
  // `pack` provides the strings (clip names); `assets` the records; handles[i] belongs to textures[i] (0 = failed upload).
  static void bind(const PackView* pack, const PackAssets* assets, const u32* handles, u32 count);
  static void unbind();
  static bool bound();
  static const PackView* pack();
  static const PackAssets& assets();
  static u32 handle(u32 textureIndex);               // renderer handle, 0 if unknown
  static u32 textureIndex(const Guid& texture);      // index into TEXR, kInvalidIndex if unknown
};

// Sort keys: sprites sit between the 3D pass and the UI. UI always draws above sprites.
constexpr u32 kSpriteSortBase = 0x1000;
constexpr u32 kUiSortBase = 0x100000;
u32 spriteSortKey(i32 sortingOrder);

struct SpriteRenderer : Component {
  REFLECT(SpriteRenderer)
  HEADER("Sprite")
  SERIALIZE_FIELD(Guid, texture, Guid())
  TOOLTIP("Source rectangle in texture pixels. Width 0 means the whole texture.")
  SERIALIZE_FIELD(i32, srcX, 0)
  SERIALIZE_FIELD(i32, srcY, 0)
  SERIALIZE_FIELD(i32, srcW, 0)
  SERIALIZE_FIELD(i32, srcH, 0)
  SERIALIZE_FIELD(Color32, color, Color32(255, 255, 255, 255))
  TOOLTIP("Higher draws on top.")
  SERIALIZE_FIELD(i32, sortingOrder, 0)
  SERIALIZE_FIELD(bool, flipX, false)
  TOOLTIP("Pixels from the sprite's top-left corner to the Transform position.")
  SERIALIZE_FIELD(i32, pivotX, 0)
  SERIALIZE_FIELD(i32, pivotY, 0)

  // Runtime only (not reflected). An animator overrides the texture and source rect.
  u32 overrideTexture = kInvalidIndex;  // TEXR index
  bool overrideSrc = false;
  i32 curX = 0, curY = 0, curW = 0, curH = 0;
  void SetFrame(u32 textureIndex, i32 x, i32 y, i32 w, i32 h) {
    overrideTexture = textureIndex; overrideSrc = true; curX = x; curY = y; curW = w; curH = h;
  }
};

// Plays an ANIM clip (sprite frames) on the SpriteRenderer of the same object.
struct SpriteAnimator : Behaviour {
  REFLECT(SpriteAnimator)
  REQUIRE_COMPONENT(SpriteRenderer)
  SERIALIZE_FIELD(Guid, clip, Guid())
  RANGE(0, 8)
  SERIALIZE_FIELD(Scalar, speed, 1.0f)
  SERIALIZE_FIELD(bool, playOnStart, true)

  // Runtime state (not reflected).
  u32 animIndex = kInvalidIndex;  // ANIM index
  Scalar clock = 0;               // seconds into the clip (scaled by `speed`)
  bool playing = false;
  bool finished = false;

  // Plays the clip with this name (PackAnimRec name). Does nothing if it is already playing. False if unknown.
  bool Play(const char* name);
  bool PlayGuid(const Guid& clipGuid);
  void Stop() { playing = false; }
  bool isPlaying() const { return playing; }
  bool isFinished() const { return finished; }
  u32 currentFrame() const;
  const char* currentClipName() const;

  void Start() override;
  void Update() override;
  void ApplyFrame();
};

// A tile grid cooked from a .tilemap.json (TMAP). Position = top-left of tile (0,0).
struct Tilemap : Component {
  REFLECT(Tilemap)
  SERIALIZE_FIELD(Guid, map, Guid())
  SERIALIZE_FIELD(Color32, tint, Color32(255, 255, 255, 255))
  SERIALIZE_FIELD(i32, sortingOrder, -100)

  const PackTilemapRec* record() const;
  u32 width() const;
  u32 height() const;
  u32 tileSize() const;  // pixels
  // 0xFFFF for empty or out of range.
  u16 TileAt(i32 tx, i32 ty) const;
  bool IsSolidTile(u16 tile) const;
  // Out-of-range cells report `outside`.
  bool IsSolidCell(i32 tx, i32 ty, bool outside) const;
  Vector2 origin() const;
};

// Collision of the Tilemap on the same object. BoxCollider2D::Move queries every enabled TilemapCollider.
struct TilemapCollider : Component {
  REFLECT(TilemapCollider)
  REQUIRE_COMPONENT(Tilemap)
  TOOLTIP("Cells outside the map count as solid walls.")
  SERIALIZE_FIELD(bool, solidOutside, true)
};

// Result of BoxCollider2D::Move.
struct Move2DResult {
  bool hitLeft = false, hitRight = false, hitUp = false, hitDown = false;
};

// Axis-aligned box in virtual pixels: top-left = Transform position + (offsetX, offsetY).
struct BoxCollider2D : Component {
  REFLECT(BoxCollider2D)
  SERIALIZE_FIELD(Scalar, offsetX, 0.0f)
  SERIALIZE_FIELD(Scalar, offsetY, 0.0f)
  SERIALIZE_FIELD(Scalar, sizeX, 16.0f)
  SERIALIZE_FIELD(Scalar, sizeY, 16.0f)
  TOOLTIP("Triggers do not block. A solid box that overlaps a trigger sends OnTriggerEnter to both objects.")
  SERIALIZE_FIELD(bool, isTrigger, false)

  Rect bounds() const;
  bool Overlaps(const BoxCollider2D& other) const { return bounds().Overlaps(other.bounds()); }
  // Moves the object by (dx, dy) in virtual pixels, X first and then Y, stopping at solid tiles.
  // Sets `grounded` when the box rests on a solid tile (also when it did not move this call).
  Move2DResult Move(Scalar dx, Scalar dy);
  // True when a solid tile lies directly under the box.
  bool CheckGround() const;

  bool grounded = false;  // runtime: result of the last Move / CheckGround
};

// ---- UI (virtual pixels, screen space; the Transform is ignored) -----------------------------------------------
struct UIText : Component {
  REFLECT(UIText)
  TOOLTIP("Font asset GUID. Null uses the engine default font.")
  SERIALIZE_FIELD(Guid, font, Guid())
  SERIALIZE_FIELD(i32, x, 0)
  SERIALIZE_FIELD(i32, y, 0)
  SERIALIZE_FIELD(Color32, color, Color32(255, 255, 255, 255))
  SERIALIZE_FIELD(i32, sortingOrder, 0)

  static constexpr u32 kMaxText = 64;
  char text[kMaxText] = {};  // runtime: set by scripts
  void SetText(const char* s);
  // "<prefix><value>" (no heap).
  void SetInt(const char* prefix, i32 value);
};

// Draws an image or, with no texture, a flat rectangle.
struct UIImage : Component {
  REFLECT(UIImage)
  SERIALIZE_FIELD(Guid, texture, Guid())
  SERIALIZE_FIELD(i32, x, 0)
  SERIALIZE_FIELD(i32, y, 0)
  SERIALIZE_FIELD(i32, w, 16)
  SERIALIZE_FIELD(i32, h, 16)
  SERIALIZE_FIELD(i32, srcX, 0)
  SERIALIZE_FIELD(i32, srcY, 0)
  SERIALIZE_FIELD(i32, srcW, 0)
  SERIALIZE_FIELD(i32, srcH, 0)
  SERIALIZE_FIELD(Color32, color, Color32(255, 255, 255, 255))
  SERIALIZE_FIELD(i32, sortingOrder, -1)
};

// A focusable button. D-pad up/down (or left/right) moves the focus between interactable buttons in pool order,
// the south button presses the focused one. A press calls Behaviour::OnUIClick on the same object and the handler
// set with UI::setClickHandler.
struct UIButton : Component {
  REFLECT(UIButton)
  SERIALIZE_FIELD(i32, x, 0)
  SERIALIZE_FIELD(i32, y, 0)
  SERIALIZE_FIELD(i32, w, 80)
  SERIALIZE_FIELD(i32, h, 16)
  SERIALIZE_FIELD(Color32, normalColor, Color32(60, 60, 80, 255))
  SERIALIZE_FIELD(Color32, focusColor, Color32(240, 160, 30, 255))
  SERIALIZE_FIELD(bool, interactable, true)
  SERIALIZE_FIELD(i32, sortingOrder, 0)
  bool isFocused() const;
};

namespace UI {
using ClickHandler = void (*)(GameObject* button);
void setClickHandler(ClickHandler handler);
// Frame step: focus movement and presses from Input::pad(). Called by GameLoop::frame.
void update(World& world);
void resetFocus();
}  // namespace UI

// Registers nothing; implementation hooks used by GameLoop and renderWorld.
namespace Gameplay2D {
// Trigger events between BoxCollider2D pairs. Called once per fixed step by GameLoop.
void fixedStep(World& world);
void resetTriggerMemory();
}  // namespace Gameplay2D

// Draws Tilemaps, SpriteRenderers and the UI (virtual pixels). No-op until GfxAssets is bound.
void renderGameplay2D(World& world, IRenderer& renderer);

}  // namespace rensga
