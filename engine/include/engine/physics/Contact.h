#pragma once

// ============================================================================
//  Contact.h - what a script is told about a collision.
//
//  THIS IS ITS OWN FILE FOR ONE REASON, and it is a C++ reason rather than a
//  design one.
//
//  scene/ScriptHooks.h works out which lifecycle hooks a script has, and one
//  of them may take a Collision2D. Inside MakeScriptHooks there is a line that
//  reads `contact.other` to serve the simpler form of the hook - and that
//  expression does not depend on the script's type, so the compiler checks it
//  while it is reading the TEMPLATE rather than when something instantiates
//  it. A forward declaration is therefore not enough: the type has to be
//  complete right there.
//
//  The alternative was for ScriptHooks.h to include physics/Collider.h, which
//  would drag five collider classes and the whole collision system into the
//  one file whose job is to ask the compiler a question about a script. Six
//  numbers in a header of their own is a much smaller thing to depend on.
// ============================================================================

#include <engine/math/Vec2.h>
#include <engine/scene/EntityId.h>

namespace eng {

class ColliderComponent;

// ---------------------------------------------------------------------------
//  Collision2D - Unity's Collision2D, with the fields that mean something in
//  a 2D engine this size.
//
//  The normal and the separation are what tell "I landed on something" from
//  "I walked into a wall", which an EntityId on its own cannot answer:
//
//      void OnCollisionEnter(const eng::Collision2D& hit) {
//          if (hit.normal.y > 0.7f) { m_onGround = true; }   // came from below
//      }
//
//  THE NORMAL ALWAYS POINTS FROM THE OTHER COLLIDER TOWARD THIS ONE, for
//  whoever is receiving the event. Both sides of a collision therefore get a
//  normal pointing at themselves, which is the only convention that reads
//  correctly in both handlers - the floor's handler and the player's would
//  otherwise disagree about which way "up" was.
// ---------------------------------------------------------------------------
struct Collision2D {
    // The other entity. ALWAYS SET, including on an exit event, where there is
    // no longer a contact to describe.
    EntityId other{};

    // POINTERS, AND NOT OWNERS. Valid for the duration of the handler and no
    // longer - the next destroy can take either of them away. Read what you
    // need and do not keep them; look the entity up through the scene instead.
    ColliderComponent* otherCollider = nullptr;
    ColliderComponent* thisCollider  = nullptr;

    Vec2 normal{};   // unit, points toward the receiver
    Vec2 point{};    // roughly where they met, in world space

    // Negative when they overlap, by that much. Zero on an exit event.
    float separation = 0.0f;

    // How fast the two were closing before the contact was resolved. Zero when
    // neither side has a rigidbody, and zero on an exit. This is the number to
    // use for "play a louder sound for a harder landing" - it is what Unity's
    // relativeVelocity is for.
    Vec2 relativeVelocity{};
};

} // namespace eng
