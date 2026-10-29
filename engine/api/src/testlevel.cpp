// The TestLevel built in code (--scene-test, unit tests). Mirrors templates/psp/Assets/Scenes/TestLevel.scene.
#include <stdio.h>
#include <string.h>

#include "rensga/Sim.h"

namespace rensga {
namespace {

const char* kCube = "00000000-0000-4000-8000-00000000c001";
const char* kSphere = "00000000-0000-4000-8000-00000000c002";
const char* kCapsule = "00000000-0000-4000-8000-00000000c003";
const char* kPlane = "00000000-0000-4000-8000-00000000c004";
const char* kRamp = "00000000-0000-4000-8000-00000000c005";

Guid guidOf(const char* text) { Guid g; Guid::parse(text, g); return g; }

bool g_ok = true;

struct Added {
  Component* c = nullptr;
  const TypeInfo* t = nullptr;
};

template <class T>
void setField(const Added& a, const char* name, const T& value) {
  for (u32 i = 0; a.c && i < a.t->fieldCount; ++i) {
    const FieldDesc& f = a.t->fields[i];
    if (strcmp(f.name, name) == 0 && f.size == sizeof(T)) {
      memcpy(reinterpret_cast<u8*>(a.c) + f.offset, &value, sizeof(T));
      return;
    }
  }
  g_ok = false;
}

Added addByName(World& w, GameObject* go, const char* type) {
  const TypeInfo* t = TypeRegistry::instance().find(type);
  if (!t) { g_ok = false; return {}; }
  return {w.addComponent(go, *t), t};
}

GameObject* make(World& w, const char* name, Vector3 pos, Vector3 scale, GameObject* parent = nullptr) {
  GameObject* go = w.createObject(name, parent);
  go->transform.localPosition = pos;
  go->transform.localScale = scale;
  return go;
}

GameObject* mesh(World& w, const char* name, const char* prim, Vector3 pos, Vector3 scale, Color32 color) {
  GameObject* go = make(w, name, pos, scale);
  MeshRenderer* mr = w.addComponent<MeshRenderer>(go);
  mr->mesh = guidOf(prim);
  mr->color = color;
  return go;
}

void box(World& w, GameObject* go, Vector3 center, Vector3 size) {
  BoxCollider* b = w.addComponent<BoxCollider>(go);
  b->center = center;
  b->size = size;
}

}  // namespace

bool buildTestLevel(World& w) {
  g_ok = true;
  GameObject* ground = mesh(w, "Ground", kPlane, {0, 0, 0}, {4, 1, 4}, Color32(90, 150, 90, 255));
  box(w, ground, {0, -0.5f, 0}, {10, 1, 10});

  struct Plat { const char* name; Vector3 pos, scale; };
  const Plat plats[] = {{"Platform1", {-4, 0.5f, 3}, {3, 1, 3}},   {"Platform2", {-8, 1.0f, 3}, {3, 2, 3}},
                        {"Platform3", {-8, 0.5f, 8}, {4, 1, 2}},   {"Platform4", {4, 0.75f, 8}, {3, 1.5f, 3}},
                        {"Platform5", {10, 0.5f, 8}, {3, 1, 3}}};
  for (const Plat& p : plats) {
    GameObject* go = mesh(w, p.name, kCube, p.pos, p.scale, Color32(170, 140, 100, 255));
    box(w, go, {0, 0, 0}, {1, 1, 1});
  }

  // Ramp: wedge mesh (rises toward +Z) with stair-step colliders no taller than stepOffset.
  mesh(w, "Ramp", kRamp, {6, 0.6f, -4}, {4, 1.2f, 3}, Color32(200, 120, 80, 255));
  for (int i = 0; i < 4; ++i) {
    char name[16];
    snprintf(name, sizeof(name), "RampStep%d", i);
    Scalar h = 0.3f * static_cast<Scalar>(i + 1);
    GameObject* go = make(w, name, {6, h * 0.5f, -7.0f + 0.75f + 1.5f * static_cast<Scalar>(i)}, {1, 1, 1});
    box(w, go, {0, 0, 0}, {4, h, 1.5f});
  }

  GameObject* player = mesh(w, "Player", kCapsule, {0, 1, 0}, {1, 1, 1}, Color32(60, 120, 220, 255));
  player->guid = guidOf("00000000-0000-4000-9000-000000000001");
  CharacterController* cc = w.addComponent<CharacterController>(player);
  cc->height = 2.0f; cc->radius = 0.5f; cc->gravity = -20.0f; cc->stepOffset = 0.35f;
  Added pc = addByName(w, player, "PlayerController");
  setField<Scalar>(pc, "speed", 6.0f);
  setField<Scalar>(pc, "jumpSpeed", 8.0f);
  GameObject* head = make(w, "Head", {0, 0.8f, 0.15f}, {0.6f, 0.6f, 0.6f}, player);
  MeshRenderer* hm = w.addComponent<MeshRenderer>(head);
  hm->mesh = guidOf(kSphere);
  hm->color = Color32(240, 210, 170, 255);

  const Vector3 pickupPos[5] = {{0, 1, 4}, {-4, 2, 3}, {-8, 3, 3}, {4, 2.5f, 8}, {6, 2.2f, -1.7f}};
  for (int i = 0; i < 5; ++i) {
    char name[16];
    snprintf(name, sizeof(name), "Pickup%d", i + 1);
    GameObject* go = mesh(w, name, kSphere, pickupPos[i], {0.6f, 0.6f, 0.6f}, Color32(255, 220, 40, 255));
    SphereCollider* sc = w.addComponent<SphereCollider>(go);
    sc->radius = 1.0f;
    sc->isTrigger = true;
    Added pk = addByName(w, go, "Pickup");
    setField<Scalar>(pk, "spinSpeed", 90.0f);
  }

  GameObject* cam = make(w, "Camera", {0, 5, -8}, {1, 1, 1});
  w.addComponent<Camera>(cam);
  Added fc = addByName(w, cam, "FollowCamera");
  setField<Guid>(fc, "target", player->guid);
  setField<Scalar>(fc, "distance", 8.0f);
  setField<Scalar>(fc, "height", 4.0f);
  setField<Scalar>(fc, "orbitSpeed", 90.0f);

  GameObject* light = make(w, "Light", {0, 10, 0}, {1, 1, 1});
  w.addComponent<DirectionalLight>(light);

  GameObject* gm = make(w, "GameManager", {0, 0, 0}, {1, 1, 1});
  Added counter = addByName(w, gm, "PickupCounter");
  setField<i32>(counter, "total", 5);
  return g_ok;
}

}  // namespace rensga
