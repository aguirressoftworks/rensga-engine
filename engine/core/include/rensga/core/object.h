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

enum class Space : u8 { World, Self };

constexpr u32 kMaxNameLength = 32;
constexpr u32 kMaxTagLength = 24;

// Unity-style Transform. Properties are getters and setters (RULES 4). The raw local TRS values and the
// hierarchy links (underscore names) are engine internals; scripts use the methods.
struct Transform {
  Vector3 lpos_;
  Quaternion lrot_;
  Vector3 lscale_ = Vector3(1, 1, 1);

  GameObject* gameObject = nullptr;
  Transform* parent_ = nullptr;
  Transform* firstChild_ = nullptr;
  Transform* nextSibling_ = nullptr;

  // ---- local values
  Vector3 localPosition() const { return lpos_; }
  void setLocalPosition(const Vector3& p) { lpos_ = p; }
  Quaternion localRotation() const { return lrot_; }
  void setLocalRotation(const Quaternion& q) { lrot_ = q; }
  Vector3 localScale() const { return lscale_; }
  void setLocalScale(const Vector3& s) { lscale_ = s; }

  // ---- matrices
  Matrix4 localMatrix() const { return Matrix4::TRS(lpos_, lrot_, lscale_); }
  Matrix4 localToWorld() const { return parent_ ? parent_->localToWorld() * localMatrix() : localMatrix(); }
  Matrix4 localToWorldMatrix() const { return localToWorld(); }
  Matrix4 worldToLocalMatrix() const { return localToWorld().inverse(); }

  // ---- world values
  Vector3 position() const { return parent_ ? localToWorld().translationPart() : lpos_; }
  void setPosition(const Vector3& p) {
    lpos_ = parent_ ? parent_->localToWorld().inverse().transformPoint(p) : p;
  }
  Quaternion rotation() const { return parent_ ? (parent_->rotation() * lrot_) : lrot_; }
  void setRotation(const Quaternion& q) {
    lrot_ = parent_ ? (parent_->rotation().conjugate() * q).normalized() : q;
  }
  Vector3 lossyScale() const {
    Vector3 s = lscale_;
    for (const Transform* t = parent_; t; t = t->parent_)
      s = Vector3(s.x * t->lscale_.x, s.y * t->lscale_.y, s.z * t->lscale_.z);
    return s;
  }
  Vector3 forward() const { return rotation() * Vector3(0, 0, 1); }
  Vector3 right() const { return rotation() * Vector3(1, 0, 0); }
  Vector3 up() const { return rotation() * Vector3(0, 1, 0); }

  // ---- space conversion
  Vector3 TransformPoint(const Vector3& p) const { return localToWorld().transformPoint(p); }
  Vector3 InverseTransformPoint(const Vector3& p) const { return worldToLocalMatrix().transformPoint(p); }
  Vector3 TransformDirection(const Vector3& d) const { return rotation() * d; }
  Vector3 InverseTransformDirection(const Vector3& d) const { return rotation().conjugate() * d; }

  // ---- movement
  // Translate moves along the transform's own axes by default (Space::Self), like Unity.
  void Translate(const Vector3& delta, Space space = Space::Self) {
    setPosition(position() + (space == Space::Self ? rotation() * delta : delta));
  }
  // Rotates by Euler degrees (x, y, z).
  void Rotate(const Vector3& eulerDegrees, Space space = Space::Self) {
    Quaternion q = Quaternion::Euler(eulerDegrees.x, eulerDegrees.y, eulerDegrees.z);
    if (space == Space::Self) lrot_ = (lrot_ * q).normalized();
    else setRotation((q * rotation()).normalized());
  }
  // Turns so that forward() points at `target` (world space).
  void LookAt(const Vector3& target, const Vector3& worldUp = Vector3(0, 1, 0));

  // ---- hierarchy
  Transform* parent() const { return parent_; }
  u32 childCount() const;
  Transform* GetChild(u32 index) const;  // null if out of range
  // Direct child by name; "a/b" walks a path. Null if not found.
  Transform* Find(const char* childName) const;
  // Re-parents (null = scene root). With worldPositionStays the world position, rotation and scale are kept,
  // otherwise the local values are kept. Refuses to parent an object under its own descendant.
  void SetParent(Transform* newParent, bool worldPositionStays = true);
  // Raw link change that keeps local values and does no activation callbacks (loader, cloner).
  void setParent(Transform* newParent);
  bool IsChildOf(const Transform* other) const {
    for (const Transform* t = parent_; t; t = t->parent_)
      if (t == other) return true;
    return false;
  }
};

class Component {
 public:
  virtual ~Component() {}
  GameObject* gameObject() const;  // defined in world.h
  Transform* transform() const;    // defined in world.h
  bool enabled() const { return enabled_; }
  void setEnabled(bool e);
  // Instantiate calls this after copying the reflected fields, for runtime-only state that must carry over.
  virtual void OnCloned(const Component* /*source*/) {}

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
  // The UIButton on this object was pressed (focus + south button).
  virtual void OnUIClick() {}
};

constexpr u32 kMaxComponentsPerObject = 8;

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
  char tag[kMaxTagLength] = {'U', 'n', 't', 'a', 'g', 'g', 'e', 'd', 0};
  u16 layer = 0;
  bool selfActive_ = true;
  bool alive = false;
  bool dying = false;
  Transform transform;
  ComponentSlot components[kMaxComponentsPerObject];
  u32 componentCount = 0;

  bool activeSelf() const { return selfActive_; }
  bool activeInHierarchy() const {
    for (const Transform* t = &transform; t; t = t->parent_)
      if (!t->gameObject->selfActive_) return false;
    return true;
  }
  void SetActive(bool active);  // world.h
  void SetTag(const char* newTag);  // world.h
  bool CompareTag(const char* other) const;

  // ---- components (world.h)
  template <class T>
  T* GetComponent() const;
  // Fills `out` (up to `capacity`) with every T on this object; returns the count found.
  template <class T>
  u32 GetComponents(T** out, u32 capacity) const;
  // This object first, then its children depth-first. Inactive objects are skipped unless includeInactive.
  template <class T>
  T* GetComponentInChildren(bool includeInactive = false) const;
  // This object first, then its parents.
  template <class T>
  T* GetComponentInParent() const;
  template <class T>
  T* AddComponent();
  Ref<GameObject> ref();

  // ---- static lookups on the active World (all fixed-capacity, no heap)
  static GameObject* Find(const char* objectName);
  static GameObject* FindWithTag(const char* objectTag);
  static u32 FindGameObjectsWithTag(const char* objectTag, GameObject** out, u32 capacity);
};

}  // namespace rensga
