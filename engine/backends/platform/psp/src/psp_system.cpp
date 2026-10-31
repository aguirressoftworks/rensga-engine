// PSP system services (F5.13, F5.15, F5.16): suspend / Home -> pause events, the system's confirm button and language, battery,
// runtime CPU clock, memory stats, the on-screen keyboard and the savedata dialog. Each is an IPlatform seam (backends.h).
//
// Utility dialogs draw through the GE, so they need a live GU context and a per-frame update between "the frame's list is finished and
// the GE is idle" and "the next endFrame swaps the buffers". The runtime main does that with updateDialogs() after
// GuRenderer::flush() (no IRenderer change). Savedata saves go to ms0:/PSP/SAVEDATA/<game id><slot>/DATA.BIN.
#include <pspdisplay.h>
#include <pspimpose.h>
#include <pspkernel.h>
#include <psppower.h>
#include <psputility.h>
#include <string.h>

#include "rensga/platform/psp.h"

namespace rensga {

// ---- system parameters --------------------------------------------------------------------------------------------------------
u32 PspPlatform::language() {
  int v = 0;
  if (sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE, &v) != PSP_SYSTEMPARAM_RETVAL_OK) return 0xFFFFFFFFu;
  // PSP: Japanese 0, English 1, then French .. Chinese like the engine's SystemLanguage (English 0, Japanese 1, French 2, ...).
  if (v == PSP_SYSTEMPARAM_LANGUAGE_JAPANESE) return 1;
  if (v == PSP_SYSTEMPARAM_LANGUAGE_ENGLISH) return 0;
  return v >= 2 && v <= 11 ? static_cast<u32>(v) : 0xFFFFFFFFu;
}

bool PspPlatform::confirmButtonEast() {
  int v = PSP_UTILITY_ACCEPT_CROSS;
  if (sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_BUTTON_SWAP, &v) != PSP_SYSTEMPARAM_RETVAL_OK) return false;
  return v == PSP_UTILITY_ACCEPT_CIRCLE;
}

// ---- battery, clock, memory ---------------------------------------------------------------------------------------------------
i32 PspPlatform::batteryPercent() {
  if (!scePowerIsBatteryExist()) return -1;
  const int p = scePowerGetBatteryLifePercent();
  return p < 0 ? -1 : (p > 100 ? 100 : p);
}

i32 PspPlatform::batteryStatus() {
  if (!scePowerIsBatteryExist()) return 0;
  if (scePowerIsBatteryCharging()) return 1;
  if (scePowerIsPowerOnline()) return scePowerGetBatteryLifePercent() >= 100 ? 4 : 3;
  return 2;
}

bool PspPlatform::setCpuClock(i32 mhz) {
  if (mhz != 222 && mhz != 333) return false;
  if (scePowerSetClockFrequency(mhz, mhz, mhz / 2) < 0) return false;
  const bool ok = scePowerGetCpuClockFrequencyInt() == mhz;
  RLOG_INFO("platform", "rensga: clock set=%d cpu=%d bus=%d %s", static_cast<int>(mhz), scePowerGetCpuClockFrequencyInt(),
            scePowerGetBusClockFrequencyInt(), ok ? "ok" : "MISMATCH");
  return ok;
}

u64 PspPlatform::systemMemoryBytes() { return memTotal_; }
u64 PspPlatform::usedSystemMemoryBytes() { return memUsed_ ? memUsed_(memUser_) : 0; }
u64 PspPlatform::graphicsMemoryBytes() { return 0x200000; }  // 2 MiB of VRAM
u64 PspPlatform::usedGraphicsMemoryBytes() { return vramProbe_ ? vramProbe_(vramUser_) : 0; }

// ---- suspend / resume / Home --------------------------------------------------------------------------------------------------
i32 PspPlatform::suspendState() {
  // The power callback thread only counts; deliver those transitions one per call (suspends first, then resumes).
  const unsigned s = suspendCount(), r = resumeCount();
  if (delivSuspends_ != s) { ++delivSuspends_; return 1; }
  if (delivResumes_ != r) { ++delivResumes_; return -1; }
  // The Home popup (the system's "quit game?" overlay) is not a power event: sceImposeGetHomePopup() is 1 while it is open.
  // Polled every 8th call: PPSSPP does not implement it and logs an error per call, and a popup lasts many frames anyway.
  if ((homePoll_++ & 7u) != 0) return 0;
  const int open = sceImposeGetHomePopup() > 0 ? 1 : 0;
  if (open != homeOpen_) {
    homeOpen_ = open;
    return open ? 1 : -1;
  }
  return 0;
}

// ---- UTF-8 <-> UTF-16 (the dialogs use UTF-16) --------------------------------------------------------------------------------------
namespace {
constexpr u32 kTextCap = 64;
alignas(16) unsigned short g_oskIn[kTextCap + 1], g_oskOut[kTextCap + 1], g_oskDesc[33];
char g_oskText[kTextCap * 4 + 1];
SceUtilityOskData g_oskData;
SceUtilityOskParams g_oskParams;

u32 utf8To16(const char* s, unsigned short* out, u32 cap) {
  u32 n = 0;
  const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
  while (*p && n + 1 < cap) {
    u32 c = *p++;
    if (c >= 0xF0 && p[0] && p[1] && p[2]) { c = ((c & 7) << 18) | ((p[0] & 63) << 12) | ((p[1] & 63) << 6) | (p[2] & 63); p += 3; }
    else if (c >= 0xE0 && p[0] && p[1]) { c = ((c & 15) << 12) | ((p[0] & 63) << 6) | (p[1] & 63); p += 2; }
    else if (c >= 0xC0 && p[0]) { c = ((c & 31) << 6) | (p[0] & 63); p += 1; }
    if (c >= 0x10000) {
      if (n + 2 >= cap) break;
      c -= 0x10000;
      out[n++] = static_cast<unsigned short>(0xD800 + (c >> 10));
      out[n++] = static_cast<unsigned short>(0xDC00 + (c & 0x3FF));
    } else {
      out[n++] = static_cast<unsigned short>(c);
    }
  }
  out[n] = 0;
  return n;
}

u32 utf16To8(const unsigned short* s, char* out, u32 cap) {
  u32 n = 0;
  for (; *s && n + 5 < cap; ++s) {
    u32 c = *s;
    if (c >= 0xD800 && c < 0xDC00 && s[1] >= 0xDC00 && s[1] < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (s[1] - 0xDC00); ++s; }
    if (c < 0x80) out[n++] = static_cast<char>(c);
    else if (c < 0x800) { out[n++] = static_cast<char>(0xC0 | (c >> 6)); out[n++] = static_cast<char>(0x80 | (c & 63)); }
    else if (c < 0x10000) { out[n++] = static_cast<char>(0xE0 | (c >> 12)); out[n++] = static_cast<char>(0x80 | ((c >> 6) & 63)); out[n++] = static_cast<char>(0x80 | (c & 63)); }
    else { out[n++] = static_cast<char>(0xF0 | (c >> 18)); out[n++] = static_cast<char>(0x80 | ((c >> 12) & 63)); out[n++] = static_cast<char>(0x80 | ((c >> 6) & 63)); out[n++] = static_cast<char>(0x80 | (c & 63)); }
  }
  out[n] = 0;
  return n;
}

void setupCommon(pspUtilityDialogCommon& b, u32 size) {
  memset(&b, 0, sizeof(b));
  b.size = size;
  int v = 0;
  b.language = sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE, &v) == 0 ? v : PSP_SYSTEMPARAM_LANGUAGE_ENGLISH;
  v = PSP_UTILITY_ACCEPT_CROSS;
  sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_BUTTON_SWAP, &v);
  b.buttonSwap = v;  // the dialog follows the same O/X setting the game reads through confirmButtonEast()
  b.graphicsThread = 0x11;
  b.accessThread = 0x13;
  b.fontThread = 0x12;
  b.soundThread = 0x10;
}
}  // namespace

// ---- on-screen keyboard -------------------------------------------------------------------------------------------------------------
bool PspPlatform::beginTextInput(const char* initial, const char* title, u32 maxLen) {
  if (osk_) return false;
  if (maxLen == 0 || maxLen > kTextCap) maxLen = kTextCap;
  utf8To16(initial ? initial : "", g_oskIn, kTextCap + 1);
  utf8To16(title ? title : "", g_oskDesc, 33);
  memset(g_oskOut, 0, sizeof(g_oskOut));
  memset(&g_oskData, 0, sizeof(g_oskData));
  g_oskData.language = PSP_UTILITY_OSK_LANGUAGE_DEFAULT;
  g_oskData.lines = 1;
  g_oskData.unk_24 = 1;
  g_oskData.inputtype = PSP_UTILITY_OSK_INPUTTYPE_ALL;
  g_oskData.desc = g_oskDesc;
  g_oskData.intext = g_oskIn;
  g_oskData.outtextlength = kTextCap;
  g_oskData.outtextlimit = static_cast<int>(maxLen);
  g_oskData.outtext = g_oskOut;
  memset(&g_oskParams, 0, sizeof(g_oskParams));
  setupCommon(g_oskParams.base, sizeof(g_oskParams));
  g_oskParams.datacount = 1;
  g_oskParams.data = &g_oskData;
  const int r = sceUtilityOskInitStart(&g_oskParams);
  if (r < 0) {
    RLOG_WARN("platform", "rensga: osk InitStart failed 0x%08x", static_cast<unsigned>(r));
    return false;
  }
  osk_ = 1;
  oskResult_ = 0;
  RLOG_INFO("platform", "rensga: osk open max=%u", static_cast<unsigned>(maxLen));
  return true;
}

i32 PspPlatform::pollTextInput(char* out, u32 outSize) {
  if (oskResult_ == 1 && out && outSize) {
    strncpy(out, g_oskText, outSize - 1);
    out[outSize - 1] = 0;
  }
  return oskResult_;
}

void PspPlatform::updateDialogs() {
  if (!osk_) return;
  switch (sceUtilityOskGetStatus()) {
    case PSP_UTILITY_DIALOG_VISIBLE: sceUtilityOskUpdate(1); break;
    case PSP_UTILITY_DIALOG_QUIT: sceUtilityOskShutdownStart(); break;
    case PSP_UTILITY_DIALOG_NONE: {
      osk_ = 0;
      if (g_oskData.result == PSP_UTILITY_OSK_RESULT_CANCELLED) {
        oskResult_ = 2;
      } else {
        // CHANGED: the typed text. UNCHANGED: the initial text.
        utf16To8(g_oskData.result == PSP_UTILITY_OSK_RESULT_CHANGED ? g_oskOut : g_oskIn, g_oskText, sizeof(g_oskText));
        oskResult_ = 1;
      }
      RLOG_INFO("platform", "rensga: osk closed result=%d", static_cast<int>(g_oskData.result));
      break;
    }
    default: break;  // INIT / FINISHED: nothing to update yet
  }
}

// ---- savedata dialog ----------------------------------------------------------------------------------------------------------------
namespace {
constexpr u32 kMaxSave = 16 * 1024 + 16;
alignas(64) u8 g_saveBuf[kMaxSave];
SceUtilitySavedataParam g_save;
char g_saveNames[2][20];
PspUtilitySavedataListSaveNewData g_saveNew;
}  // namespace

i32 PspPlatform::saveDialog(const char* slot, void* data, u32 bytes, bool load) {
  if (!data || bytes == 0 || bytes > kMaxSave - 16) return 0;
  // Slot -> save name: A-Z 0-9 _ - only (the system refuses other characters), at most 19 characters.
  char name[20] = {};
  u32 n = 0;
  for (const char* c = slot ? slot : ""; *c && n < 19; ++c) {
    char ch = *c;
    if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
    if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')) ch = '_';
    name[n++] = ch;
  }
  if (!n) { name[0] = '0'; n = 1; }
  memset(g_saveBuf, 0, sizeof(g_saveBuf));
  if (!load) memcpy(g_saveBuf, data, bytes);
  memset(&g_save, 0, sizeof(g_save));
  setupCommon(g_save.base, sizeof(g_save));
  g_save.mode = saveAuto_ ? (load ? PSP_UTILITY_SAVEDATA_AUTOLOAD : PSP_UTILITY_SAVEDATA_AUTOSAVE)
                          : (load ? PSP_UTILITY_SAVEDATA_LISTLOAD : PSP_UTILITY_SAVEDATA_LISTSAVE);
  g_save.overwrite = 1;
  strncpy(g_save.gameName, RENSGA_PSP_GAME_ID, 12);
  memcpy(g_save.saveName, name, n);
  memset(g_saveNames, 0, sizeof(g_saveNames));
  memcpy(g_saveNames[0], name, n);  // entry 1 stays empty: the list ends at the first empty name
  g_save.saveNameList = g_saveNames;
  strcpy(g_save.fileName, "DATA.BIN");
  g_save.dataBuf = g_saveBuf;
  g_save.dataBufSize = (bytes + 15u) & ~15u;
  g_save.dataSize = load ? 0 : bytes;
  strncpy(g_save.sfoParam.title, "Rensga game", sizeof(g_save.sfoParam.title) - 1);
  strncpy(g_save.sfoParam.savedataTitle, "Rensga save", sizeof(g_save.sfoParam.savedataTitle) - 1);
  strncpy(g_save.sfoParam.detail, name, sizeof(g_save.sfoParam.detail) - 1);
  g_save.sfoParam.parentalLevel = 1;
  g_save.focus = PSP_UTILITY_SAVEDATA_FOCUS_LATEST;
  memset(&g_saveNew, 0, sizeof(g_saveNew));
  static char newTitle[] = "New save";
  g_saveNew.title = newTitle;
  g_save.newData = &g_saveNew;

  if (sceUtilitySavedataInitStart(&g_save) < 0) {
    RLOG_WARN("platform", "rensga: savedata InitStart failed");
    return 0;
  }
  RLOG_INFO("platform", "rensga: savedata %s %s%s mode=%s", load ? "load" : "save", RENSGA_PSP_GAME_ID, name, saveAuto_ ? "auto" : "dialog");
  for (u32 frame = 0; frame < 60u * 600u; ++frame) {  // ten minutes at most
    if (pump_) pump_(pumpUser_);
    else sceDisplayWaitVblankStart();
    const int st = sceUtilitySavedataGetStatus();
    if (st == PSP_UTILITY_DIALOG_VISIBLE) sceUtilitySavedataUpdate(1);
    else if (st == PSP_UTILITY_DIALOG_QUIT) sceUtilitySavedataShutdownStart();
    else if (st == PSP_UTILITY_DIALOG_NONE) break;
  }
  const int result = g_save.base.result;
  RLOG_INFO("platform", "rensga: savedata done result=0x%08x size=%u", static_cast<unsigned>(result), static_cast<unsigned>(g_save.dataSize));
  if (result != 0) return 0;
  if (!load) return static_cast<i32>(bytes);
  u32 got = g_save.dataSize;
  if (got == 0 || got > bytes) got = got ? bytes : 0;
  if (got) memcpy(data, g_saveBuf, got);
  return static_cast<i32>(got);
}

}  // namespace rensga
