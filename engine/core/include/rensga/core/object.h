// MVP-12: GameObject, Transform hierarchy, Component and Behaviour (Unity-style lifecycle).
// Raw pointers returned by the API (gameObject(), transform(), GetComponent) are transient:
// use them inside one callback and keep a Ref<T> for anything longer-lived.
#pragma once
#include <string.h>

#include "rensga/core/behaviour_events.h"
#include "rensga/core/coroutine.h"
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

  // `go->transform()` reads like `transform()` inside a Behaviour (a Transform pointer) and `go->transform.position()` keeps working.
  Transform* operator()() { return this; }
  const Transform* operator()() const { return this; }

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
  // Engine internals for handles (coroutines, Invoke, Event): the owning World and the registry type id.
  World* ownerWorld() const { return world_; }
  u32 typeId() const { return typeId_; }

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
  // F5.13: the application lost or regained the foreground (PSP: suspend or the Home popup; Android: pause). Sent to every
  // started behaviour with true when pausing and false on resume (Unity: MonoBehaviour.OnApplicationPause).
  virtual void OnApplicationPause(bool /*paused*/) {}
  // Editor callbacks (API 11). OnValidate runs after an inspector edit, Reset on Add Component and the Reset menu, both on a
  // detached editor-side instance (gameObject is null: touch only your own fields). The gizmo callbacks draw with the
  // Gizmos API in the Scene view.
  virtual void OnValidate() {}
  virtual void Reset() {}
  virtual void OnDrawGizmos() {}
  virtual void OnDrawGizmosSelected() {}
  // Contacts (sent by the fixed step of the physics modules; `other` is the object touched).
  // 3D triggers send OnTriggerEnter / OnTriggerExit. 2D triggers (BoxCollider2D.isTrigger) send OnTriggerEnter /
  // OnTriggerExit and also the *2D names, so Unity-style 2D scripts work; override one pair, not both.
  // Two overlapping solid BoxCollider2D send OnCollisionEnter2D / OnCollisionExit2D to both objects.
  virtual void OnTriggerEnter(GameObject* /*other*/) {}
  virtual void OnTriggerExit(GameObject* /*other*/) {}
  virtual void OnTriggerEnter2D(GameObject* /*other*/) {}
  virtual void OnTriggerExit2D(GameObject* /*other*/) {}
  virtual void OnCollisionEnter2D(GameObject* /*other*/) {}
  virtual void OnCollisionExit2D(GameObject* /*other*/) {}
  // F6 (declared by the contract, sent by the Stage 1 agents; until then nothing calls them):
  //  - 3D physics (A2): OnCollisionEnter / Stay / Exit with a Collision after the fixed step, OnTriggerStay for objects that
  //    stay inside a trigger. Both objects of a pair get the callback; a Rigidbody pair or a Rigidbody against static colliders
  //    sends the collision events, trigger colliders send the trigger events.
  //  - 2D physics (A2): OnTriggerStay2D (GameObject*) and the Collision2D overloads of OnCollisionEnter2D / Stay2D / Exit2D. The
  //    GameObject* forms above stay: override the one you like, the engine calls both.
  //  - Animation (B1): OnAnimationEvent for every event of a property clip that fires on this object.
  virtual void OnCollisionEnter(const Collision& /*collision*/) {}
  virtual void OnCollisionStay(const Collision& /*collision*/) {}
  virtual void OnCollisionExit(const Collision& /*collision*/) {}
  virtual void OnTriggerStay(GameObject* /*other*/) {}
  virtual void OnTriggerStay2D(GameObject* /*other*/) {}
  virtual void OnCollisionEnter2D(const Collision2D& /*collision*/) {}
  virtual void OnCollisionStay2D(const Collision2D& /*collision*/) {}
  virtual void OnCollisionExit2D(const Collision2D& /*collision*/) {}
  virtual void OnAnimationEvent(const AnimationEvent& /*event*/) {}
  // The UIButton on this object was pressed (focus + south button).
  virtual void OnUIClick() {}

  // ---- coroutines (API 4, see coroutine.h): StartCoroutine(Wait(1).Then(f)) or a step function
  template <class F>
  Coroutine StartCoroutine(F f) {
    if constexpr (detail::IsChain<F>::value) {
      if (f.overflowed()) return Coroutine();  // chain longer than kMaxChainSteps: the builder refused a step
    }
    return detail::startCoroutine(this, CoroutineBody(f));
  }
  void StopCoroutine(const Coroutine& c) { detail::stopCoroutine(c, this); }
  void StopAllCoroutines() { detail::stopAllCoroutines(this); }

  // ---- Invoke (API 4): scaled-time timers that follow this Behaviour; no heap. The method must be void().
  template <class T>
  void Invoke(void (T::*method)(), Scalar delay) {
    u8 b[16] = {};
    memcpy(b, &method, sizeof(method));
    detail::invoke(this, &detail::callMember<T>, b, sizeof(method), delay, 0);
  }
  template <class T>
  void InvokeRepeating(void (T::*method)(), Scalar delay, Scalar repeatRate) {
    u8 b[16] = {};
    memcpy(b, &method, sizeof(method));
    detail::invoke(this, &detail::callMember<T>, b, sizeof(method), delay, repeatRate > 0 ? repeatRate : 0.0001f);
  }
  template <class T>
  void CancelInvoke(void (T::*method)()) {
    u8 b[16] = {};
    memcpy(b, &method, sizeof(method));
    detail::cancelInvoke(this, b, sizeof(method));
  }
  template <class T>
  bool IsInvoking(void (T::*method)()) const {
    u8 b[16] = {};
    memcpy(b, &method, sizeof(method));
    return detail::isInvoking(this, b, sizeof(method));
  }
  // By name through the method registry (METHOD(Name) in the class, see reflect_method.h).
  void Invoke(const char* methodName, Scalar delay) { detail::invokeByName(this, methodName, delay, 0); }
  void InvokeRepeating(const char* methodName, Scalar delay, Scalar repeatRate) {
    detail::invokeByName(this, methodName, delay, repeatRate > 0 ? repeatRate : 0.0001f);
  }
  void CancelInvoke(const char* methodName) { detail::cancelInvokeByName(this, methodName); }
  void CancelInvoke() { detail::cancelAllInvokes(this); }
  bool IsInvoking(const char* methodName) const { return detail::isInvokingByName(this, methodName); }
  bool IsInvoking() const { return detail::isInvokingAny(this); }
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
  bool dontDestroyOnLoad = false;  // runtime: survives SceneManager loads (World::dontDestroyOnLoad)
  i16 sceneIndex = -1;             // F6 (A3): SCNE index of the scene that created this object; -1 = none (made at run time, or DontDestroyOnLoad)
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
