// F4.08 part 2 (Editor 7): the 2D toolset UI. Tile Palette window, tile painting in the 2D Scene view, the Sprite Editor
// window and the inspector extras (sprite picker, "Open Tile Palette"). The models are editor/core's TilemapEdit and
// SpriteSlices; this layer only draws them and turns the mouse into strokes.
//
// Undo: every TilemapEdit records its commands on the open scene's own UndoStack (SceneDocument::undoStack()), so Ctrl+Z
// walks scene edits and tile edits in the order they were made. The TilemapEdits live here (by tilemap asset GUID) for
// the whole project session; the Scene view draws them instead of the file (Scene2DResources::setLiveTilemaps).
// Dirty tilemaps are written with the scene (File > Save Scene) and before Play (in editor or on console).
#pragma once
#include <imgui.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "app.h"
#include "rensga/editor/inspector_model.h"
#include "rensga/editor/sprite_slices.h"
#include "rensga/editor/tilemap_edit.h"

namespace rensga {
namespace editor {

enum class TileBrush { Paint, Erase, Rect, Fill, Rule };
const char* tileBrushName(TileBrush b);

struct TileTools {
  std::map<std::string, std::unique_ptr<TilemapEdit>> maps;  // by tilemap asset GUID; entries are never freed while commands may point at them
  std::map<std::string, long long> stamps;                    // file time of each edit when it was loaded or saved
  std::set<std::string> bad;                                  // tilemap assets that could not be read (reported once)
  double stampCheck = 0;
  bool showPalette = false;
  bool paintInScene = true;       // LMB in the 2D Scene view paints (off: it selects, as always)
  std::string activeGuid;         // tilemap asset the palette edits
  std::string lastSelObject;      // scene selection the palette last followed
  TileBrush brush = TileBrush::Paint;
  int tile = 0;                   // tile id of the paint, rect and fill brushes
  std::string ruleGuid;           // *.ruletile.json asset of the rule brush
  RuleTile rule;
  bool ruleOk = false;
  std::string ruleError;
  int zoom = 3;                   // tileset view scale
  std::string message;            // last problem, shown in the palette

  // New Tilemap dialog
  bool openNew = false;
  char newName[96] = "Level";
  std::string newTileset;         // texture asset GUID
  int newTileSize = 16, newW = 40, newH = 15;

  // Scene view stroke
  bool stroking = false, strokeErase = false, strokeRect = false;
  std::string strokeMap;
  int lastX = 0, lastY = 0, startX = 0, startY = 0;
  bool hoverValid = false;
  int hoverX = 0, hoverY = 0;

  // --ui-event tiledrag: a scripted mouse stroke, one step per frame
  struct Sim {
    bool active = false;
    int phase = 0;
    std::vector<std::pair<int, int>> cells;
  } sim;
};

TileTools& tileTools();
// Points Scene2DResources at the asset database and at the live tilemap edits (idempotent; the Scene view and the palette call it).
void bindScene2dResources(App& a);

// ---- tilemap edits ----
// The TilemapEdit of a tilemap asset: loaded on first use, bound to the scene's undo stack. nullptr when the asset is unknown or unreadable.
TilemapEdit* tileEdit(App& a, const std::string& tilemapGuid);
const TilemapEdit* tileEditLoaded(const std::string& tilemapGuid);  // no side effects (Scene2DResources)
std::string tilemapGuidOfObject(App& a, const std::string& objectGuid);  // Tilemap.map of the object, "" when none
int saveDirtyTilemaps(App& a);                                    // writes every modified tilemap; returns how many were written
void tileToolsTick(App& a);                                       // once per frame: follow the selection, close stray strokes, free clean edits
void tileToolsReset();                                            // project closed: drop every edit (the scene's undo stack is gone)
void openTilePalette(App& a, const std::string& tilemapGuid, bool newDialog = false);
std::string newTilemapAsset(App& a, const std::string& name, const std::string& tilesetGuid, int tileSize, int w, int h);  // GUID, "" on error

// ---- Scene view ----
struct TilePaintView {
  ImVec2 p0, size;        // the Scene view's image rect (screen px)
  M4 view, proj;
  bool hot = false;       // the mouse is over the view and not over its overlay widgets
};
// True when LMB in the view paints (palette open, Paint toggle on, 2D mode, a tilemap target, not playing).
bool tilePaintingActive(App& a, const Scene2D& s2d);
void tileSceneInput(App& a, const Scene2D& s2d, const TilePaintView& v);   // strokes, hover preview and the status label
void tileSimFrame(App& a);                                      // main loop, before ImGui::NewFrame: feeds the scripted mouse
bool tileSimHolds();

void drawTilePalette(App& a);
bool tileUiHook(App& a, const std::string& act, const std::string& arg);   // palette: brush: tile: ruletile: painttile: ...

// ---- Sprite Editor (panels/sprite_editor.cpp) ----
void openSpriteEditor(App& a, const std::string& textureGuid);
void drawSpriteEditor(App& a);
bool spriteUiHook(App& a, const std::string& act, const std::string& arg);   // spriteeditor: slicegrid: sliceapply: ...
bool spriteEditorFocused();   // the Sprite Editor has the keyboard: Ctrl+Z / Delete are its own, not the scene's
void spriteSimFrame(App& a);   // main loop, before ImGui::NewFrame: feeds the scripted mouse of --ui-event spritedrag
bool spriteSimHolds();

// ---- inspector extras (panels/drawers/component_extras.cpp) ----
// Extra widgets under a component's reflected fields: the sprite picker of SpriteRenderer, "Open Tile Palette" of Tilemap.
void drawComponentExtras(App& a, const std::string& type, const std::vector<ComponentRef>& refs, bool multi);
// Sets the slice `name` of the texture on the SpriteRenderers `refs` as one undo step. False when there is no such slice.
// --ui-event spritepick: opens the SpriteRenderer sprite picker popup on the next Inspector draw (screenshots).
bool& spritePickerOpenRequest();
bool applySliceByName(App& a, const std::vector<ComponentRef>& refs, const std::string& name);

// Pixel art: draws with nearest-neighbour sampling (ImGui's default sampler is linear) until imageNearestEnd().
inline void imageNearestBegin(ImDrawList* dl) { dl->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest, nullptr); }
inline void imageNearestEnd(ImDrawList* dl) { dl->AddCallback(ImGui::GetPlatformIO().DrawCallback_ResetRenderState, nullptr); }

// PNG size from the file header (no decode); false when it is not a PNG.
bool pngFileSize(const std::filesystem::path& file, int& w, int& h);

}  // namespace editor
}  // namespace rensga
