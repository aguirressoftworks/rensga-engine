// Shared by Pickup and PickupCounter.
#pragma once
#include <rensga/Rensga.h>

struct PickupCounter : rensga::Behaviour {
  REFLECT(PickupCounter)
  RANGE(1, 64)
  SERIALIZE_FIELD(rensga::i32, total, 5)

  static inline rensga::i32 collected = 0;
  static inline rensga::i32 totalShown = 0;

  void Awake() override {
    collected = 0;
    totalShown = total;
  }
  static void Collect() {
    ++collected;
    rensga::Debug::Log("rensga: pickup %d/%d", static_cast<int>(collected), static_cast<int>(totalShown));
  }
  // HUD pips: one quad per pickup, gold once collected.
  void LateUpdate() override {
    for (rensga::i32 i = 0; i < total; ++i) {
      rensga::Color32 c = i < collected ? rensga::Color32(255, 220, 40, 255) : rensga::Color32(60, 60, 60, 255);
      rensga::Hud::Quad(8 + i * 14, 8, 10, 10, c);
    }
  }
};
