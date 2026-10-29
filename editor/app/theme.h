// Rensga brand theme for Dear ImGui. Colors come from docs/rensga-engine/brand/palette.json (embedded at
// build time); nothing here hard-codes a hex value.
#pragma once
#include <SDL.h>
#include <imgui.h>

namespace rensga {
namespace editor {

struct Fonts {
  ImFont* ui = nullptr;      // Inter regular
  ImFont* uiBold = nullptr;  // Inter semibold
  ImFont* mono = nullptr;    // JetBrains Mono
  float uiSize = 15.0f, monoSize = 14.0f, wordmarkSize = 16.0f;
};

bool loadBrand();                       // parses the embedded palette; false (and a log line) if broken
void applyBrandTheme();                 // ImGui colors + metrics from the brand
Fonts loadBrandFonts();                 // Inter + JetBrains Mono, falls back to the ImGui default font
ImVec4 brandToken(const char* name);    // dark-theme token ("primary", "on-primary", "danger", ...)
ImVec4 brandRamp(const char* ref);      // palette ramp step ("pequi.600")
void drawPequi(ImDrawList* dl, ImVec2 topLeft, float size);  // the pequi icon, vector, size x size
SDL_Surface* makePequiIcon(int size);   // CPU-rasterized icon for the window (caller frees)

}  // namespace editor
}  // namespace rensga
