#include <rensga/Rensga.h>

#include "GameState.h"

using namespace rensga;

// Run, jump and animate. Movement runs in FixedUpdate; BoxCollider2D::Move resolves tile collisions.
struct PlayerController2D : Behaviour {
  REFLECT(PlayerController2D)
  HEADER("Movement (pixels per second)")
  RANGE(0, 600)
  SERIALIZE_FIELD(Scalar, moveSpeed, 200.0f)
  RANGE(0, 1200)
  SERIALIZE_FIELD(Scalar, jumpSpeed, 560.0f)
  RANGE(0, 4000)
  SERIALIZE_FIELD(Scalar, gravity, 1400.0f)

  Scalar axis = 0;
  Scalar vy = 0;
  bool jumpQueued = false;

  void Start() override { GameState::StartRun(); }

  void Update() override {
    Scalar x = Input::GetAxis2("Move").x + Input::GetAxis2("Pad").x;
    axis = Mathf::Clamp(x, -1.0f, 1.0f);
    if (Input::GetButtonDown("Jump")) jumpQueued = true;
  }

  void FixedUpdate() override {
    BoxCollider2D* box = gameObject()->GetComponent<BoxCollider2D>();
    if (!box) return;
    const Scalar dt = Time::fixedDeltaTime();
    if (!GameState::won) ++GameState::fixedTicks;
    const bool canMove = !GameState::won;
    if (jumpQueued && box->grounded && canMove) {
      vy = -jumpSpeed;
      GameState::Sfx(GameState::kSfxJump);
    }
    jumpQueued = false;
    vy += gravity * dt;
    const Scalar vx = canMove ? axis * moveSpeed : 0.0f;
    Move2DResult r = box->Move(vx * dt, vy * dt);
    if (r.hitDown || r.hitUp) vy = 0;

    SpriteRenderer* sr = gameObject()->GetComponent<SpriteRenderer>();
    SpriteAnimator* an = gameObject()->GetComponent<SpriteAnimator>();
    if (sr && axis > 0.1f) sr->flipX = false;
    if (sr && axis < -0.1f) sr->flipX = true;
    if (an) {
      if (!box->grounded) an->Play("player_jump");
      else if (Mathf::Abs(axis) > 0.1f && canMove) an->Play("player_run");
      else an->Play("player_idle");
    }
  }
};
