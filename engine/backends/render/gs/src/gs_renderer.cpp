#include "rensga/render/gs.h"

#include <dmaKit.h>
#include <gsKit.h>

#include "rensga/core/log.h"

namespace rensga {

bool GsRenderer::init(u32, u32) {
  GSGLOBAL* g = gsKit_init_global();
  if (!g) return false;
  g->ZBuffering = GS_SETTING_OFF;
  g->DoubleBuffering = GS_SETTING_ON;
  dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
  dmaKit_chan_init(DMA_CHANNEL_GIF);
  gsKit_init_screen(g);
  gsKit_mode_switch(g, GS_ONESHOT);
  gs_ = g;
  w_ = static_cast<u32>(g->Width);
  h_ = static_cast<u32>(g->Height);
  RLOG_INFO("gs", "renderer ready %ux%u", w_, h_);
  return true;
}

void GsRenderer::beginFrame(Color32 c) {
  GSGLOBAL* g = static_cast<GSGLOBAL*>(gs_);
  gsKit_clear(g, GS_SETREG_RGBAQ(c.r, c.g, c.b, 0x80, 0x00));
}

void GsRenderer::endFrame() {
  GSGLOBAL* g = static_cast<GSGLOBAL*>(gs_);
  gsKit_queue_exec(g);
  gsKit_sync_flip(g);
}

}  // namespace rensga
