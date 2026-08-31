#pragma once

// =============================================================================
//  WEEK 6 - Mat3. Ch. 5.3.
//
//  A 3x3 matrix used for 2D transforms in HOMOGENEOUS coordinates. A 2x2
//  matrix can rotate and scale but cannot translate, because translation is
//  not a linear operation. The third row and column make translation
//  expressible as a matrix multiply, which is the entire reason transform
//  hierarchies work.
//
// =============================================================================
//  *** CONVENTION USED BY THIS ENGINE. WRITTEN DOWN BEFORE ANY CODE. ***
//
//    Storage:  ROW-MAJOR.  m[row][col]. m[0] is the first ROW.
//    Vectors:  ROW vectors. A point is [x y 1] and transforms as v' = v * M.
//    Compose:  to apply A and THEN B, write   A * B.
//
//  This matches Gregory Ch. 5.3, which uses row vectors and row-major storage
//  and is explicit about it. Matching the book means the worked examples in
//  the reading transcribe directly instead of needing transposing in your head
//  at 1am.
//
//  Consequences that follow from the choice, spelled out because they are what
//  people get wrong:
//
//    - TRANSLATION LIVES IN THE BOTTOM ROW: m[2][0], m[2][1]. (In a column
//      vector convention it lives in the right-hand column instead. If you
//      ever read engine code that puts it there, that engine uses M*v.)
//
//    - COMPOSITION READS LEFT TO RIGHT, which is the whole practical benefit:
//      LocalMatrix = Scale * Rotation * Translation means "scale it, then
//      rotate it, then move it", in that order, reading normally.
//
//    - A CHILD'S WORLD MATRIX IS  local * parentWorld.  Local first.
//
//    - ROTATION IS COUNTER-CLOCKWISE for a positive angle in a
//      y-up coordinate system. (1,0) rotated by +90 degrees is (0,1). The test
//      "a child orbits when its parent rotates" pins exactly this down: if you
//      get (0,-1), the convention and the code have drifted apart. Fix this
//      comment block first, then the code, so the two stay in agreement.
//
//  The engine's world space is Y-UP. The screen is y-down, and the single
//  place that flip happens is Camera::ViewMatrix - see Camera.h.
// =============================================================================

#include <engine/math/Vec2.h>

namespace eng {

struct Mat3 {
    // Row-major: m[row][col].
    //
    // Week 4 note: 9 floats, 36 bytes, alignment 4, no padding. This is one of
    // the structs in the sizeof audit where prediction and measurement agree
    // exactly, and it agrees for a reason worth stating - every member has the
    // same alignment, so there is nowhere for the compiler to insert anything.
    f32 m[3][3]{};

    static Mat3 Identity();
    static Mat3 Translation(Vec2 t);
    static Mat3 Rotation(f32 radians);
    static Mat3 Scaling(Vec2 s);

    // The standard TRS build, in this engine's order. Equivalent to
    // Scaling(s) * Rotation(r) * Translation(t) and written out longhand
    // because it is on the hot path of every WorldMatrix call.
    static Mat3 FromTRS(Vec2 translation, f32 radians, Vec2 scale);

    // v' = v * M. Translation APPLIES: a position moves when the space moves.
    Vec2 TransformPoint(Vec2 point) const;

    // Translation does NOT apply: a direction or a velocity is unaffected by
    // moving the origin. Two functions because the type system cannot tell a
    // position from a direction when both are Vec2 - and getting this wrong
    // makes velocities drift when an object moves, which is a memorable bug.
    Vec2 TransformVector(Vec2 direction) const;

    // Inverse of an AFFINE transform-rotate-scale matrix.
    //
    // ASSUMED (and asserted): the third column is (0, 0, 1) - i.e. this is an
    // affine transform, not a projective one. That assumption turns a general
    // 3x3 inverse into a 2x2 inverse plus a translated offset, which is a
    // handful of operations instead of a cofactor expansion.
    //
    // Also assumed: the 2x2 part is invertible (no zero scale on an axis). A
    // zero-scale matrix has no inverse; this asserts and returns identity so
    // that a release build degrades to "camera does nothing" rather than
    // producing NaNs that spread.
    Mat3 Inverse() const;

    // Extraction helpers used by the sprite renderer and the Inspector.
    Vec2 GetTranslation() const;
    Vec2 GetScale() const;
    f32  GetRotation() const;   // radians, counter-clockwise
};

// C = A * B, meaning "apply A, then B" under this engine's row-vector
// convention.
Mat3 operator*(const Mat3& a, const Mat3& b);

bool ApproxEqual(const Mat3& a, const Mat3& b, f32 epsilon = kDefaultEpsilon);

} // namespace eng
