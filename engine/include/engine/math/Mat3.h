#pragma once

// ============================================================================
//  Mat3.h - a 3x3 matrix, used to move, turn and resize things in 2D.
//
//  WHY A 3x3 MATRIX FOR A 2D GAME
//  A 2x2 matrix can rotate and scale, but it cannot MOVE anything: multiplying
//  (0, 0) by any 2x2 matrix always gives (0, 0) back. The trick is to pretend
//  every 2D point is really a 3D point with a 1 stuck on the end - (x, y, 1) -
//  and use a 3x3 matrix. Now the extra row can add an offset, so moving,
//  turning and resizing are all "multiply by a matrix" and they can be
//  combined by multiplying the matrices together. That is what makes a parent/
//  child transform hierarchy possible at all.
//
//  ==========================================================================
//  THE CONVENTION THIS ENGINE USES. Read this before touching any of it.
//
//    Storage:      m[row][col]. m[0] is the first ROW.
//    Points:       written as a ROW, [x y 1], transformed as  v' = v * M.
//    Combining:    to do A and THEN B, write  A * B.
//
//  Two consequences that trip everybody up at least once:
//
//    * THE MOVE (translation) LIVES IN THE BOTTOM ROW: m[2][0], m[2][1].
//      Plenty of engines and textbooks put it in the right-hand COLUMN
//      instead. Those use the other convention (M * v, points as columns).
//      Both are correct; mixing them is not.
//
//    * COMBINING READS LEFT TO RIGHT, which is the reason for the choice:
//         Scaling * Rotation * Translation
//      means "shrink it, then turn it, then move it", in that order, read
//      normally. Under the other convention you read that line backwards.
//
//  The world is Y-UP: increasing y goes up the screen, and a positive rotation
//  turns anticlockwise. The screen itself is y-down. The one and only place
//  those are reconciled is Camera::ViewMatrix - see Camera.h.
//  ==========================================================================
// ============================================================================

#include <engine/math/Vec2.h>

namespace eng {

struct Mat3 {
    // A plain 3-by-3 array of floats, indexed [row][column].
    // The `{}` gives every element the value 0 when a Mat3 is declared without
    // one, so there is no such thing as a Mat3 full of leftover memory.
    float m[3][3]{};

    // The "do nothing" matrix. Multiplying by it leaves a point where it was.
    static Mat3 Identity();

    // Each of these builds a matrix that does ONE thing.
    static Mat3 Translation(Vec2 t);      // move by t
    static Mat3 Rotation(float radians);  // turn anticlockwise
    static Mat3 Scaling(Vec2 s);          // resize

    // Builds scale-then-rotate-then-move in one step. Exactly the same result
    // as Scaling(s) * Rotation(r) * Translation(t), written out longhand
    // because every object's world position goes through this every frame.
    static Mat3 FromTRS(Vec2 translation, float radians, Vec2 scale);

    // Transforms a POSITION. The move part applies: if the space slides right,
    // so does the point.
    Vec2 TransformPoint(Vec2 point) const;

    // Transforms a DIRECTION (a velocity, an offset, a "which way is up").
    // The move part does NOT apply: sliding the whole world sideways does not
    // change which way something is facing.
    //
    // There are two functions because C++ cannot tell a position from a
    // direction - both are just a Vec2. Using the wrong one makes velocities
    // drift as an object moves, which is a memorable afternoon of debugging.
    Vec2 TransformVector(Vec2 direction) const;

    // The matrix that undoes this one. Used by the camera to turn a mouse
    // position on screen back into a position in the world.
    //
    // This only works for matrices built out of moves, turns and resizes,
    // which is all this engine ever makes. A matrix with a scale of zero on an
    // axis cannot be undone (the information is gone), so that case returns
    // the identity and logs a warning rather than producing NaNs.
    Mat3 Inverse() const;

    // Pulls the individual pieces back out of a finished matrix. The Inspector
    // uses these to show position/rotation/scale boxes.
    Vec2  GetTranslation() const;
    Vec2  GetScale() const;
    float GetRotation() const;   // radians, anticlockwise
};

// Combines two matrices. Under this engine's convention, `a * b` means
// "do a, then b".
Mat3 operator*(const Mat3& a, const Mat3& b);

// Compares every element with a tolerance, for the same reason Vec2 has an
// ApproxEqual: matrix arithmetic almost never lands on exact values.
bool ApproxEqual(const Mat3& a, const Mat3& b, float epsilon = kDefaultEpsilon);

} // namespace eng
