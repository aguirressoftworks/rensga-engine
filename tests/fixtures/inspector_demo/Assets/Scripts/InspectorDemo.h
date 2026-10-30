#pragma once
#include <rensga/Rensga.h>
using namespace rensga;

// Every field type and every editor attribute, for the inspector screenshots.
struct InspectorDemo : Behaviour {
  REFLECT(InspectorDemo)
  EXECUTE_IN_EDIT_MODE()
  REQUIRE_COMPONENT(Camera)
  HEADER("Movement")
  TOOLTIP("Units per second")
  RANGE(0, 20)
  SERIALIZE_FIELD(Scalar, speed, 5)
  SERIALIZE_FIELD(i32, lives, 3)
  SERIALIZE_FIELD(bool, canJump, true)
  SPACE(12)
  HEADER("Transform data")
  SERIALIZE_FIELD(Vector3, offset, Vector3(0, 1, 0))
  SERIALIZE_FIELD(Quaternion, facing, Quaternion())
  SERIALIZE_FIELD(Color32, tint, Color32(255, 160, 0, 255))
  SERIALIZE_FIELD(Guid, target, Guid())
  HEADER("Events")
  SERIALIZE_FIELD(UnityEvent, onDone, {})
  HIDE_IN_INSPECTOR()
  SERIALIZE_FIELD(i32, hiddenCounter, 0)
  CONTEXT_MENU("Fill Lives")
  void FillLives() { lives = 9; }
  METHOD(Boost)
  METHOD(SetSpeed)
  void Boost() { speed = 15; }
  void SetSpeed(Scalar v) { speed = v; }
  void OnValidate() override { if (lives < 0) lives = 0; }
  void Reset() override { lives = 7; }
};
