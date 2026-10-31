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
#include "rensga/core/unity_event.h"

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
  // Flag byte of a tileset tile (PackTileFlag bits, see pack_format.h); 0 for empty or unknown tiles.
  u8 TileFlags(u16 tile) const;
  // Full solid block (flag bit 0). Slopes and one-way tiles are not "solid" here.
  bool IsSolidTile(u16 tile) const;
  // Out-of-range cells report `outside`.
  bool IsSolidCell(i32 tx, i32 ty, bool outside) const;
  Vector2 origin() const;
};

// Collision of the Tilemap on the same object. BoxCollider2D::Move queries every enabled TilemapCollider2D.
struct TilemapCollider2D : Component {
  REFLECT(TilemapCollider2D)
  REQUIRE_COMPONENT(Tilemap)
  TOOLTIP("Cells outside the map count as solid walls.")
  SERIALIZE_FIELD(bool, solidOutside, true)
};

// Layer collision matrix (32 x 32), from ProjectSettings/Physics2D.json (pack section PH2D); the default is that
// every layer collides with every other. The layer of an object is GameObject::layer (low 5 bits). Tilemap
// collision and trigger events check it.
namespace Physics2D {
constexpr u32 kLayerCount = 32;
void IgnoreLayerCollision(u32 layerA, u32 layerB, bool ignore = true);
bool GetIgnoreLayerCollision(u32 layerA, u32 layerB);
void resetMatrix();                              // all layers collide
void loadFromPack(const PackView* pack);         // PH2D if present, else resetMatrix
bool layersCollide(const GameObject* a, const GameObject* b);
}  // namespace Physics2D

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
  // Slopes: while grounded the box follows the slope surface up and down (the foot point is the middle of the
  // box's bottom edge). One-way tiles block only a box that falls onto their top edge. Slopes and one-way tiles
  // support from above only. Tilemaps on layers that do not collide with this object's layer are skipped.
  // Sets `grounded` when the box rests on a tile (also when it did not move this call).
  Move2DResult Move(Scalar dx, Scalar dy);
  // True when a solid tile, one-way tile or slope lies directly under the box.
  bool CheckGround() const;
  // Ignores the one-way tile the box stands on until the box has fallen past it. False if it stands on none.
  bool DropThrough();

  bool grounded = false;  // runtime: result of the last Move / CheckGround
  // Runtime: Down held + South pressed while standing on a one-way tile calls DropThrough inside Move.
  bool autoDropThrough = true;
  bool dropping = false;
  Scalar dropY = 0;  // bottom edge where the drop started
};

// Follows `target` (a GameObject GUID) with a dead zone and clamps the view to the bounds. renderGameplay2D scrolls
// tilemaps and sprites by the first enabled Camera2D (UI stays in screen space). The view is the rectangle
// (viewX, viewY, viewWidth, viewHeight) in virtual pixels; it is updated in LateUpdate, or call UpdateView().
struct Camera2D : Behaviour {
  REFLECT(Camera2D)
  HEADER("Follow")
  SERIALIZE_FIELD(Guid, target, Guid())
  TOOLTIP("Pixels added to the target's Transform position to get the point that is kept in view (for example half the sprite size).")
  SERIALIZE_FIELD(Scalar, targetOffsetX, 0.0f)
  SERIALIZE_FIELD(Scalar, targetOffsetY, 0.0f)
  TOOLTIP("The target moves freely inside this box at the middle of the view; the camera moves only when it leaves it.")
  SERIALIZE_FIELD(Scalar, deadZoneW, 32.0f)
  SERIALIZE_FIELD(Scalar, deadZoneH, 24.0f)
  HEADER("Bounds")
  TOOLTIP("Clamp the view to a rectangle.")
  SERIALIZE_FIELD(bool, useBounds, false)
  TOOLTIP("Object with a Tilemap whose area is the bounds. Null uses the rectangle below.")
  SERIALIZE_FIELD(Guid, boundsTilemap, Guid())
  SERIALIZE_FIELD(Scalar, boundsX, 0.0f)
  SERIALIZE_FIELD(Scalar, boundsY, 0.0f)
  SERIALIZE_FIELD(Scalar, boundsW, 320.0f)
  SERIALIZE_FIELD(Scalar, boundsH, 240.0f)
  HEADER("View")
  TOOLTIP("Round the view position to whole pixels.")
  SERIALIZE_FIELD(bool, pixelSnap, true)
  TOOLTIP("0 uses the renderer's virtual resolution.")
  SERIALIZE_FIELD(Scalar, viewWidth, 0.0f)
  SERIALIZE_FIELD(Scalar, viewHeight, 0.0f)

  Scalar viewX = 0, viewY = 0;  // runtime: top-left of the view in world pixels

  void LateUpdate() override { UpdateView(); }
  void UpdateView();
  Scalar effectiveViewWidth() const;
  Scalar effectiveViewHeight() const;
  // The first enabled, active Camera2D of the active World (null if none).
  static Camera2D* main();
  // Set by renderGameplay2D from the renderer; used when viewWidth/viewHeight are 0 (initially 320 x 240).
  static void setDefaultViewSize(Scalar w, Scalar h);
};

// ---- UI (virtual pixels, screen space; the Transform is ignored) -----------------------------------------------
// F4.01 anchors. Every UI element has an `anchor` (a UIAnchor value, TopLeft = 0 = the old behaviour). The anchor names
// a point of the current view (TopLeft = (0, 0), Center = (w/2, h/2), BottomRight = (w, h), ...) AND the point of the
// element that sits on it (its pivot), so x / y are offsets in virtual pixels from that point: a BottomRight text with
// x = -16, y = -12 ends 16 px from the right edge and 12 px above the bottom edge, whatever the view size. One scene fits
// 640x448 and 480x272. Reflection has no enum field type, so the field is an i32.
namespace UIAnchor {
constexpr i32 TopLeft = 0, Top = 1, TopRight = 2, Left = 3, Center = 4, Right = 5, BottomLeft = 6, Bottom = 7, BottomRight = 8;
constexpr i32 kCount = 9;
}  // namespace UIAnchor

namespace UI {
// Top-left corner of a w x h element with the given anchor, offset (x, y) and view size. Unknown anchors act as TopLeft.
Rect resolveRect(i32 anchor, i32 x, i32 y, i32 w, i32 h, i32 viewW, i32 viewH);
// Width in pixels of the widest line of `text` in `font` (advance widths; '\n' starts a new line) and its line count.
i32 textWidth(const PackFontRec& font, const char* text);
i32 textHeight(const PackFontRec& font, const char* text);
}  // namespace UI

struct UIText : Component {
  REFLECT(UIText)
  TOOLTIP("Font asset GUID. Null uses the engine default font.")
  SERIALIZE_FIELD(Guid, font, Guid())
  TOOLTIP("0 TopLeft, 1 Top, 2 TopRight, 3 Left, 4 Center, 5 Right, 6 BottomLeft, 7 Bottom, 8 BottomRight. x / y are offsets from this point of the view; the element's own pivot is the same point.")
  RANGE(0, 8)
  SERIALIZE_FIELD(i32, anchor, 0)
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
  TOOLTIP("0 TopLeft, 1 Top, 2 TopRight, 3 Left, 4 Center, 5 Right, 6 BottomLeft, 7 Bottom, 8 BottomRight. x / y are offsets from this point of the view; the element's own pivot is the same point.")
  RANGE(0, 8)
  SERIALIZE_FIELD(i32, anchor, 0)
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

// A focusable button. D-pad up/down (or left/right) moves the focus between interactable buttons in reading order on
// screen (top to bottom, then left to right; the first one has the focus when a menu opens), the south button presses the focused one. A press calls Behaviour::OnUIClick on the same object and the handler
// set with UI::setClickHandler.
struct UIButton : Component {
  REFLECT(UIButton)
  TOOLTIP("0 TopLeft, 1 Top, 2 TopRight, 3 Left, 4 Center, 5 Right, 6 BottomLeft, 7 Bottom, 8 BottomRight. x / y are offsets from this point of the view; the element's own pivot is the same point.")
  RANGE(0, 8)
  SERIALIZE_FIELD(i32, anchor, 0)
  SERIALIZE_FIELD(i32, x, 0)
  SERIALIZE_FIELD(i32, y, 0)
  SERIALIZE_FIELD(i32, w, 80)
  SERIALIZE_FIELD(i32, h, 16)
  SERIALIZE_FIELD(Color32, normalColor, Color32(60, 60, 80, 255))
  SERIALIZE_FIELD(Color32, focusColor, Color32(240, 160, 30, 255))
  SERIALIZE_FIELD(bool, interactable, true)
  SERIALIZE_FIELD(i32, sortingOrder, 0)
  // F6 (appended; D1): Unity's Navigation. Automatic = the reading-order focus above. Explicit = the select* targets (UIButton,
  // Slider or Toggle objects) in the four directions, a null target means "stay". None = the pad never moves the focus off it.
  APPENDED_FIELDS()
  SERIALIZE_ENUM(navigation, 0, "Automatic|Explicit|None")
  SERIALIZE_FIELD(Guid, selectOnUp, Guid())
  SERIALIZE_FIELD(Guid, selectOnDown, Guid())
  SERIALIZE_FIELD(Guid, selectOnLeft, Guid())
  SERIALIZE_FIELD(Guid, selectOnRight, Guid())
  TOOLTIP("Called when the button is pressed, in addition to Behaviour::OnUIClick.")
  SERIALIZE_FIELD(UnityEvent, onClick, {})
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
