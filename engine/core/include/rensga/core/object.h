// MVP-12: GameObject, Transform hierarchy, Component and Behaviour (Unity-style lifecycle).
// Raw pointers returned by the API (gameObject(), transform(), GetComponent) are transient:
// use them inside one callback and keep a Ref<T> for anything longer-lived.
#pragma once
#include "rensga/core/guid.h"
#include "rensga/core/math.h"
#include "rensga/core/ref.h"
#include "rensga/core/reflect.h"
#include "rensga/core/types.h"

namespace rensga {

class World;
struct GameObject;

struct Transform {
  Vector3 localPosition;
  Quaternion localRotation;
  Vector3 localScale = Vector3(1, 1, 1);

  GameObject* gameObject = nullptr;
  Transform* parent = nullptr;
  Transform* firstChild = nullptr;
  Transform* nextSibling = nullptr;

  Matrix4 localMatrix() const { return Matrix4::TRS(localPosition, localRotation, localScale); }
  Matrix4 localToWorld() const {
    return parent ? parent->localToWorld() * localMatrix() : localMatrix();
  }
  Vector3 position() const { return parent ? localToWorld().translationPart() : localPosition; }
  void setPosition(const Vector3& p) {
    localPosition = parent ? parent->localToWorld().inverse().transformPoint(p) : p;
  }
  Quaternion rotation() const { return parent ? (parent->rotation() * localRotation) : localRotation; }
  void setRotation(const Quaternion& q) {
    localRotation = parent ? (parent->rotation().conjugate() * q).normalized() : q;
  }
  Vector3 forward() const { return rotation() * Vector3(0, 0, 1); }
  Vector3 right() const { return rotation() * Vector3(1, 0, 0); }
  Vector3 up() const { return rotation() * Vector3(0, 1, 0); }
  // Moves in world space.
  void Translate(const Vector3& delta) { setPosition(position() + delta); }
  // Rotates by Euler degrees in local space.
  void Rotate(const Vector3& eulerDegrees) {
    localRotation = (localRotation * Quaternion::Euler(eulerDegrees.x, eulerDegrees.y, eulerDegrees.z)).normalized();
  }
  void setParent(Transform* newParent);  // keeps local values
};

class Component {
 public:
  virtual ~Component() {}
  GameObject* gameObject() const;  // defined in world.h
  Transform* transform() const;    // defined in world.h
  bool enabled() const { return enabled_; }
  void setEnabled(bool e);

 private:
  friend class World;
  World* world_ = nullptr;
  u32 ownerIndex_ = kInvalidIndex;
  u32 slot_ = kInvalidIndex;
  u32 typeId_ = kInvalidIndex;
  bool enabled_ = true;
};

// Scripts derive from Behaviour. Order: Awake, OnEnable, Start, FixedUpdate, Update, LateUpdate,
// OnDisable, OnDestroy.
class Behaviour : public Component {
 public:
  virtual void Awake() {}
  virtual void OnEnable() {}
  virtual void Start() {}
  virtual void FixedUpdate() {}
  virtual void Update() {}
  virtual void LateUpdate() {}
  virtual void OnDisable() {}
  virtual void OnDestroy() {}
  virtual void OnTriggerEnter(GameObject* /*other*/) {}
};

constexpr u32 kMaxComponentsPerObject = 8;
constexpr u32 kMaxNameLength = 32;

struct ComponentSlot {
  u32 typeId = kInvalidIndex;
  u32 slot = kInvalidIndex;
};

struct GameObject {
  World* world = nullptr;
  u32 index = kInvalidIndex;
  u32 generation = 1;
  Guid guid;
  char name[kMaxNameLength] = {};
  u16 layer = 0;
  bool activeSelf = true;
  bool alive = false;
  bool dying = false;
  Transform transform;
  ComponentSlot components[kMaxComponentsPerObject];
  u32 componentCount = 0;

  bool activeInHierarchy() const {
    for (const Transform* t = &transform; t; t = t->parent)
      if (!t->gameObject->activeSelf) return false;
    return true;
  }
  void SetActive(bool active);  // world.h
  template <class T>
  T* GetComponent() const;  // world.h
  template <class T>
  T* AddComponent();  // world.h
  Ref<GameObject> ref();  // world.h
};

}  // namespace rensga
