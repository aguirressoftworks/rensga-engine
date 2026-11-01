# Physics (API 6, F6.08)

Console-light 3D physics. It is meant for gameplay (crates, balls, knock-back, triggers, hit scans), not for a physics showcase. It runs
the same way on every profile: Scalar arithmetic only (no libm in the simulation), fixed capacities, no allocation per frame, and results
that are bit-identical between the PC build and the PSP (`tools/ci/determinism.sh phys`).

## What is simulated

| Part | Behaviour |
| --- | --- |
| Colliders | `BoxCollider` (oriented: it follows the object's rotation and lossy scale), `SphereCollider` (appended `center`), `CapsuleCollider` (X, Y or Z axis). A `CharacterController` is a collider too (an axis-aligned box of its radius and height). |
| `Rigidbody` | mass, drag, `useGravity`, `isKinematic`, `constraints` (freeze position axes; rotation never changes), `AddForce(v, ForceMode)`, `velocity()`/`setVelocity`, `MovePosition`, `Sleep`/`WakeUp`. |
| Response | sequential impulses, 8 iterations, normal and Coulomb friction impulses, speculative contacts (no bounce when landing), a split position correction of 4 iterations. **No rotational dynamics**: a body never rotates, so one contact point per collider pair is enough. Friction 0.4 and bounciness 0 for every contact (no `PhysicMaterial` yet). |
| Kinematic bodies | moved by `MovePosition` or by their Transform; they push dynamic bodies with the velocity they had in the step. |
| Sleeping | a body under `sleepThreshold` for 30 steps sleeps; it wakes on any API call, when a moving body touches it, or when it loses its support. |
| Layers | `Physics.json` `ignore` pairs (or `Physics::IgnoreLayerCollision`), 32 layers, applied to collisions and to trigger events. |
| Gravity | `Physics::gravity()`, default (0, -9.81, 0), from `Physics.json`. |

## Callbacks

After every fixed step, for every pair of colliders where at least one belongs to a Rigidbody or a CharacterController:
`OnCollisionEnter/Stay/Exit(const Collision&)` for solid pairs and `OnTriggerEnter/Stay/Exit(GameObject*)` when one collider is a trigger. Both
objects get the callback. `Collision` has `gameObject` (the other object), `relativeVelocity` (this minus the other, before the solve), `impulse` and up to 4
`contacts` (point, normal from the other collider toward this one). A pair is touching when the surfaces are closer than 5 mm; a trigger pair overlaps when
they intersect. Callbacks go to the object that owns the collider (a child collider does not notify its Rigidbody parent). Exit is not sent when
the other object was destroyed. 2D: `OnTriggerStay2D`, `OnCollisionStay2D(const Collision2D&)` and `Collision2D` overloads of Enter/Exit; the older
`GameObject*` forms still fire.

A CharacterController against a Box/Sphere trigger keeps its old Enter/Exit path (`physics.cpp`); Stay is added by the new step.

## Queries

`Physics::Raycast` (3 overloads), `RaycastNonAlloc` (nearest first), `Linecast`, `SphereCast`, `OverlapSphere`, `CheckSphere`; `Physics2D::Raycast`,
`OverlapCircle/Box` (+ `All`). `LayerMask`, `QueryTriggerInteraction` (`UseGlobal` follows `queriesHitTriggers`). A ray or cast that starts inside a collider does not
report it (Unity). 2D queries test `BoxCollider2D` rectangles and the full solid cells of `TilemapCollider2D` (slopes and one-way tiles are not hit).

## CharacterController

Unchanged against axis-aligned boxes. New: `slopeLimit` (default 45, appended), `groundNormal`; rotated boxes, spheres, capsules and every collider
on a Rigidbody block the controller and ground it when the contact normal is within the slope limit.

## Limits

256 colliders and 128 bodies per step (extra ones are ignored and counted in `physics3d::stats().dropped`), 512 contacts, 256 reported pairs, objects with index >= 2048 take no part in
events. No continuous collision detection: the broadphase is swept, the narrowphase is discrete, so a fast thin body can tunnel. Query ranges are capped at 1000 m.

## Cooking

`ProjectSettings/Physics.json` -> `PHY3` (gravity, queriesHitTriggers, one 32-bit mask per layer, optional `bounceThreshold` and `sleepThreshold`). `ignore` pairs may use layer
indices or the layer names of `TagManager.json`. Without `Physics.json` the pack has no `PHY3` section and the defaults apply.
