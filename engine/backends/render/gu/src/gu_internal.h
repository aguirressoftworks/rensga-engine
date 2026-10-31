// F4.04: storage shared by gu_renderer.cpp and gu_2d.cpp (PSP only).
#pragma once
#include "rensga/render/gu.h"

namespace rensga {
// Display list size: two of these live in BSS. The 2D and 3D passes stop drawing (and count it) before the list could overflow.
constexpr u32 kGuListBytes = 128 * 1024;
constexpr u32 kGuListMargin = 2048;  // room for the finish command and the last matrix updates
void guInitSpriteStorage(GuSpriteQueue& q);
GuVertex2D* guSpriteVertices(u32 frame);  // double buffered by frame parity
GuRun* guSpriteRuns();
u32 guStaticBytes();  // BSS held by the renderer (lists, sprite queue, vertex buffers)
}  // namespace rensga
