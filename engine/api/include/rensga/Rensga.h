// MVP-29: the one header game scripts include. Depends on engine/core only, never on a backend.
//
//   Behaviour (Awake, OnEnable, Start, FixedUpdate, Update, LateUpdate, OnDisable, OnDestroy,
//   OnTriggerEnter), GameObject (Find, tags, GetComponent<T> and friends), Transform (position, rotation,
//   hierarchy, Translate, Rotate, LookAt), Instantiate / Destroy, Application, Time::deltaTime() / fixedDeltaTime(), Input::GetAxis / GetButton /
//   GetButtonDown by action name, Debug::Log, Ref<T>, and the REFLECT / SERIALIZE_FIELD / RANGE /
//   HEADER macros. Game code uses Scalar, never float.
//   F2.05 gameplay: SpriteRenderer, SpriteAnimator, Tilemap, TilemapCollider, BoxCollider2D (Move), UIText,
//   UIImage, UIButton (OnUIClick), Camera2D, Physics2D::IgnoreLayerCollision, DontDestroyOnLoad (Object.h),
//   SceneManager::LoadScene, Save::SaveFile / LoadFile / SaveInt / LoadInt.
//   F3.05 events: StartCoroutine(Wait(1).Then(f)) / step functions, Invoke / InvokeRepeating, Event<Args>, UnityEvent, METHOD(Name).
#pragma once
#include "rensga/core/application.h"
#include "rensga/core/components.h"
#include "rensga/core/debug.h"
#include "rensga/core/event.h"
#include "rensga/core/gameplay.h"
#include "rensga/core/gizmos.h"
#include "rensga/core/guid.h"
#include "rensga/core/input.h"
#include "rensga/core/loop.h"
#include "rensga/core/math.h"
#include "rensga/core/object.h"
#include "rensga/core/ref.h"
#include "rensga/core/reflect.h"
#include "rensga/core/save.h"
#include "rensga/core/scene_manager.h"
#include "rensga/core/time.h"
#include "rensga/core/unity_event.h"
#include "rensga/core/world.h"

// Physics (engine/api): static-box world queries and the CharacterController are in
// rensga/Physics.h.
#include "rensga/Object.h"
#include "rensga/Physics.h"
#include "rensga/Hud.h"
#include "rensga/Audio.h"
