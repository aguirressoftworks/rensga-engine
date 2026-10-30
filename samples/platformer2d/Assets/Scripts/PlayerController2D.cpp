#include <rensga/Rensga.h>

#include "GameState.h"

using namespace rensga;

// Run, jump and animate. Movement runs in FixedUpdate; BoxCollider2D::Move resolves tile collisions.
// Enemies ask for a hit (knockback + one HP) or a bounce through GameState; the room's demo pilot can replace the pad.
struct PlayerController2D : Behaviour {
  REFLECT(PlayerController2D)
  HEADER("Movement (pixels per second)")
  RANGE(0, 600)
  SERIALIZE_FIELD(Scalar, moveSpeed, 200.0f)
  RANGE(0, 1200)
  SERIALIZE_FIELD(Scalar, jumpSpeed, 560.0f)
  RANGE(0, 4000)
  SERIALIZE_FIELD(Scalar, gravity, 1400.0f)
  HEADER("Combat")
  RANGE(0, 1200)
  SERIALIZE_FIELD(Scalar, stompBounce, 380.0f)
  RANGE(0, 1200)
  SERIALIZE_FIELD(Scalar, knockbackSpeed, 220.0f)
  RANGE(0, 600)
  SERIALIZE_FIELD(i32, invulnerableTicks, 90)

  Scalar axis = 0;
  Scalar vy = 0;
  bool jumpQueued = false;
  bool prevBotJump = false;
  i32 knockTicks = 0;
  Scalar knockDir = 0;

  void Start() override {
    GameState::StartRun();
    if (Transform* t = &gameObject()->transform) {
      GameState::checkpointX = t->position().x;
      GameState::checkpointY = t->position().y;
    }
  }

  void Update() override {
    if (GameState::botActive) {
      axis = Mathf::Clamp(GameState::botAxis, -1.0f, 1.0f);
      if (GameState::botJump && !prevBotJump) jumpQueued = true;
      prevBotJump = GameState::botJump;
      return;
    }
    Scalar x = Input::GetAxis2("Move").x + Input::GetAxis2("Pad").x;
    axis = Mathf::Clamp(x, -1.0f, 1.0f);
    if (Input::GetButtonDown("Jump")) jumpQueued = true;
  }

  void FixedUpdate() override {
    BoxCollider2D* box = gameObject()->GetComponent<BoxCollider2D>();
    if (!box) return;
    const Scalar dt = Time::fixedDeltaTime();
    if (!GameState::won) ++GameState::fixedTicks;
    if (GameState::respawnRequest) {  // out of HP: back to the last save point
      gameObject()->transform.setPosition(Vector3(GameState::checkpointX, GameState::checkpointY, 0));
      vy = 0;
      knockTicks = 0;
      GameState::hp = GameState::kMaxHp;
      GameState::hurtTicks = invulnerableTicks;
      GameState::respawnRequest = false;
    }
    if (GameState::hurtTicks > 0) --GameState::hurtTicks;
    if (GameState::hurtRequest != 0 && GameState::hurtTicks == 0 && !GameState::won) {
      knockDir = static_cast<Scalar>(GameState::hurtRequest);
      knockTicks = 10;
      vy = -300.0f;
      GameState::hurtTicks = invulnerableTicks;
      --GameState::hp;
      GameState::Sfx(GameState::kSfxHit);
      if (GameState::hp <= 0) {
        GameState::respawnRequest = true;
        GameState::Message("TRY AGAIN");
      } else {
        GameState::Message("OUCH  HP ", GameState::hp);
      }
      Debug::Log("rensga: player hit hp=%d", static_cast<int>(GameState::hp));
    }
    GameState::hurtRequest = 0;
    if (GameState::bounceRequest) {
      vy = -stompBounce;
      GameState::bounceRequest = false;
    }
    const bool canMove = !GameState::won;
    if (jumpQueued && box->grounded && canMove) {
      vy = -jumpSpeed;
      GameState::Sfx(GameState::kSfxJump);
    }
    jumpQueued = false;
    vy += gravity * dt;
    Scalar vx = canMove ? axis * moveSpeed : 0.0f;
    if (knockTicks > 0) {
      vx = knockDir * knockbackSpeed;
      --knockTicks;
    }
    Move2DResult r = box->Move(vx * dt, vy * dt);
    if (r.hitDown || r.hitUp) vy = 0;
    GameState::playerVy = vy;

    SpriteRenderer* sr = gameObject()->GetComponent<SpriteRenderer>();
    SpriteAnimator* an = gameObject()->GetComponent<SpriteAnimator>();
    if (sr) {
      if (axis > 0.1f) sr->flipX = false;
      if (axis < -0.1f) sr->flipX = true;
      const bool blink = GameState::hurtTicks > 0 && ((GameState::hurtTicks / 4) % 2) == 0;
      sr->color = Color32(255, 255, 255, blink ? 90 : 255);
    }
    if (an) {
      if (!box->grounded) an->Play("player_jump");
      else if (Mathf::Abs(axis) > 0.1f && canMove) an->Play("player_run");
      else an->Play("player_idle");
    }
  }
};
