// F6 contract (C0): per-renderer material overrides without duplicating the Material (Unity's MaterialPropertyBlock). A
// renderer that has a block draws with its color (multiplied into the material color) and texture instead of the material's own,
// everything else stays shared. Runtime only (not reflected, never cooked); Instantiate copies it. B2 (Stage 2, API 7) makes the
// GL, GU and GS renderers honour it; until then setting a block changes nothing on screen.
#pragma once
#include "rensga/core/guid.h"
#include "rensga/core/math.h"

namespace rensga {

struct MaterialPropertyBlock {
  Color32 color = Color32(255, 255, 255, 255);
  Guid texture;  // a texture asset of the pack; null = the material's texture
  bool hasColor = false;
  bool hasTexture = false;

  void SetColor(Color32 c) { color = c; hasColor = true; }
  void SetTexture(const Guid& t) { texture = t; hasTexture = !t.isNull(); }
  void Clear() { *this = MaterialPropertyBlock(); }
  bool isEmpty() const { return !hasColor && !hasTexture; }
};

}  // namespace rensga
