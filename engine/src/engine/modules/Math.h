#pragma once

// ============================================================================
//  Math.h - vectors, matrices, transforms, shape overlap tests and random numbers.
//
//  Each section below was its own file until the engine was reorganised; the
//  banner at the top of each one still explains that piece on its own.
// ============================================================================

#include <cmath>
#include <random>
#include <vector>

// ============================================================================
//  Vec2.h - a 2D point or direction: two floats, x and y.
//
//  This is the type used for every position, size, velocity and offset in the
//  engine. If something in a 2D game has a location, it is stored in a Vec2.
//
//  WHY EVERYTHING IS DEFINED IN THE HEADER
//  Normally a class is declared in a .h and implemented in a .cpp. Not here.
//  Every function below is one or two arithmetic operations, and the cost of
//  *calling* a function is larger than the cost of the work it does. Putting
//  the bodies in the header lets the compiler paste them straight into the
//  caller (this is called inlining) and the call disappears entirely.
//
//  WHY `constexpr` IS ON ALMOST EVERYTHING
//  `constexpr` tells the compiler "this can be worked out while compiling, if
//  the inputs are known then". So `Vec2{3, 4} + Vec2{1, 1}` in your source
//  becomes the literal value (4, 5) in the finished program - no addition
//  happens while the game runs.
//
//  WHY <cmath> IS INCLUDED
//  It is the C++ standard maths header, and it is where std::sqrt, std::cos,
//  std::sin, std::atan2 and std::fabs come from. There is no reason to write
//  those by hand: the standard library versions are correct, fast, and
//  available on every platform.
// ============================================================================


namespace eng {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float inX, float inY) : x(inX), y(inY) {}

    // ---- operators that CHANGE this vector -------------------------------
    // These are member functions because the thing on the left of `+=` is
    // always a Vec2, which is exactly what a member function requires.
    constexpr Vec2& operator+=(const Vec2& rhs) { x += rhs.x; y += rhs.y; return *this; }
    constexpr Vec2& operator-=(const Vec2& rhs) { x -= rhs.x; y -= rhs.y; return *this; }
    constexpr Vec2& operator*=(float scalar)    { x *= scalar; y *= scalar; return *this; }
    constexpr Vec2& operator/=(float scalar)    { x /= scalar; y /= scalar; return *this; }

    // `= default` asks the compiler to write == and != for us by comparing
    // every member. This is EXACT equality, which is rarely what you want for
    // floats - see ApproxEqual near the bottom of this file.
    friend constexpr bool operator==(const Vec2&, const Vec2&) = default;

    // The length of the vector, squared. This exists so that COMPARING two
    // distances never has to compute a square root:
    //
    //     if (a.LengthSquared() < b.LengthSquared())   // same answer,
    //     if (a.Length()        < b.Length())          // more work
    //
    // Both lines order the vectors identically, because squaring never changes
    // the order of non-negative numbers. Prefer the first.
    constexpr float LengthSquared() const { return x * x + y * y; }
    float           Length() const        { return std::sqrt(LengthSquared()); }

    // A vector pointing the same way but exactly 1 unit long.
    //
    // A vector of length zero has no direction, so there is nothing correct to
    // return. Dividing by zero here would NOT crash - floating point produces
    // "inf" or "NaN" instead, and those values then spread silently through
    // every later calculation until an object simply vanishes from the screen
    // with no error message anywhere. Returning (0, 0) is also wrong, but it
    // is wrong in one visible place instead of everywhere downstream.
    static constexpr float kNormalizeEpsilon = 1e-8f;

    Vec2 Normalized() const {
        const float lengthSq = LengthSquared();
        if (lengthSq < kNormalizeEpsilon) {
            return Vec2{0.0f, 0.0f};
        }
        // One division and two multiplies instead of two divisions.
        const float inverse = 1.0f / std::sqrt(lengthSq);
        return Vec2{x * inverse, y * inverse};
    }

    // Naming rule used throughout the engine: a name ending in -ed returns a
    // NEW value and leaves the original alone (Normalized); the bare verb
    // changes the object in place (Normalize).
    void Normalize() { *this = Normalized(); }

    // Turned 90 degrees anticlockwise. Two lines, and it comes up constantly
    // in 2D geometry - it is how you get the "sideways" direction from a
    // "forwards" one.
    constexpr Vec2 Perpendicular() const { return Vec2{-y, x}; }

    static constexpr Vec2 Zero()  { return Vec2{0.0f, 0.0f}; }
    static constexpr Vec2 One()   { return Vec2{1.0f, 1.0f}; }
    static constexpr Vec2 UnitX() { return Vec2{1.0f, 0.0f}; }
    static constexpr Vec2 UnitY() { return Vec2{0.0f, 1.0f}; }
};

// ---- operators that PRODUCE a new vector -----------------------------------
// These are free functions rather than members. The reason is the third line:
// `2.0f * v` has a float on the left, and a member function's left-hand side
// is always its own class. Once one of them has to be free, they all are, so
// that the whole family looks the same.
constexpr Vec2 operator+(const Vec2& a, const Vec2& b) { return Vec2{a.x + b.x, a.y + b.y}; }
constexpr Vec2 operator-(const Vec2& a, const Vec2& b) { return Vec2{a.x - b.x, a.y - b.y}; }
constexpr Vec2 operator-(const Vec2& v)                { return Vec2{-v.x, -v.y}; }
constexpr Vec2 operator*(const Vec2& v, float s)       { return Vec2{v.x * s, v.y * s}; }
constexpr Vec2 operator*(float s, const Vec2& v)       { return Vec2{v.x * s, v.y * s}; }
constexpr Vec2 operator/(const Vec2& v, float s)       { return Vec2{v.x / s, v.y / s}; }

// Multiplies x by x and y by y. Used for non-uniform scaling ("twice as wide,
// the same height"). This is NOT the dot product.
constexpr Vec2 Scale(const Vec2& a, const Vec2& b) { return Vec2{a.x * b.x, a.y * b.y}; }

// The dot product. Positive when the two vectors point roughly the same way,
// zero when they are at right angles, negative when they oppose.
constexpr float Dot(const Vec2& a, const Vec2& b) { return a.x * b.x + a.y * b.y; }

// In 2D the cross product is a single number, not a vector. Its SIGN says
// which side of `a` the vector `b` lies on, which makes it the standard tool
// for "is this point left or right of that line?"
constexpr float Cross(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }

constexpr float DistanceSquared(const Vec2& a, const Vec2& b) { return (b - a).LengthSquared(); }
inline    float Distance(const Vec2& a, const Vec2& b)        { return (b - a).Length(); }

// Linear interpolation: t = 0 gives a, t = 1 gives b, t = 0.5 gives the midpoint.
constexpr Vec2 Lerp(const Vec2& a, const Vec2& b, float t) { return a + (b - a) * t; }

// ---- comparing floats ------------------------------------------------------
//
// `==` on floats asks whether two numbers have identical bit patterns, and
// arithmetic that should mathematically give the same answer very often does
// not. Rotating (1, 0) by 90 degrees produces an x of about -0.000000044,
// not 0. Use these instead of == for anything that came out of a calculation.
inline constexpr float kDefaultEpsilon = 1e-4f;

inline bool ApproxEqual(float a, float b, float epsilon = kDefaultEpsilon) {
    return std::fabs(a - b) <= epsilon;
}

inline bool ApproxEqual(const Vec2& a, const Vec2& b, float epsilon = kDefaultEpsilon) {
    return ApproxEqual(a.x, b.x, epsilon) && ApproxEqual(a.y, b.y, epsilon);
}

// ---- angles ----------------------------------------------------------------
//
// The engine works in RADIANS everywhere. Degrees appear in exactly two
// places: the Inspector, because people think in degrees, and the call into
// SDL that draws a rotated sprite, because SDL's API asks for degrees.
inline constexpr float kPi       = 3.14159265358979323846f;
inline constexpr float kTwoPi    = kPi * 2.0f;
inline constexpr float kDegToRad = kPi / 180.0f;
inline constexpr float kRadToDeg = 180.0f / kPi;

// Builds a vector pointing at `radians`, `length` units long.
inline Vec2 FromAngle(float radians, float length = 1.0f) {
    return Vec2{std::cos(radians) * length, std::sin(radians) * length};
}

// The angle a vector points in. std::atan2 is used rather than std::atan
// because it takes x and y separately and therefore knows which quadrant the
// vector is in; plain atan(y/x) cannot tell (1,1) from (-1,-1).
inline float AngleOf(const Vec2& v) { return std::atan2(v.y, v.x); }

} // namespace eng


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


// ============================================================================
//  Transform2D.h - where an object is, how it is turned, and how big it is.
//
//  This is the same idea as Unity's Transform component. Every entity in the
//  engine has exactly one, and it holds three things:
//
//      position   where it sits
//      rotation   which way it faces, in radians
//      scale      how large it is
//
//  PARENTS AND CHILDREN
//  A transform can have a parent. When it does, its position/rotation/scale
//  are measured RELATIVE TO THAT PARENT rather than to the world. Move the
//  parent and the children come along; turn the parent and the children orbit
//  it. This is how a turret stays on a tank, or a wheel stays on a car,
//  without any code having to keep them in sync.
//
//  Because of that there are two versions of every question:
//      LocalPosition()  - where am I relative to my parent?
//      WorldPosition()  - where am I actually, in the world?
//
//  TWO RULES THIS CLASS ENFORCES
//
//  1. WHEN A PARENT IS DESTROYED, ITS CHILDREN ARE NOT.
//     They are handed back to the world, keeping the position they were
//     already visibly at. Destroying them instead would mean this class owned
//     their lifetime, and their lifetime already belongs to the entity that
//     holds them. Moving them would make debris jump across the screen the
//     moment the ship it came from was deleted.
//
//  2. NOTHING CAN BE ITS OWN ANCESTOR.
//     Asking for a world position walks up the parent chain, so a loop in that
//     chain would walk forever and freeze the game with no error message.
//     SetParent checks for it and refuses.
// ============================================================================



namespace eng {

class Transform2D {
public:
    Transform2D() = default;
    ~Transform2D();

    // Copying is switched off with `= delete`, which makes any attempt to copy
    // a compiler error instead of a runtime surprise.
    //
    // The reason: a Transform2D is a node in a tree. If you copied one, would
    // the copy have the same parent? The same children? Would the children now
    // have two parents? There is no answer that is not surprising, so the type
    // simply refuses to express the question.
    Transform2D(const Transform2D&)            = delete;
    Transform2D& operator=(const Transform2D&) = delete;

    // ---- relative to the parent ------------------------------------------
    Vec2  LocalPosition() const { return m_position; }
    float LocalRotation() const { return m_rotation; }   // radians, anticlockwise
    Vec2  LocalScale()    const { return m_scale; }

    void SetLocalPosition(Vec2 position) { m_position = position; }
    void SetLocalRotation(float radians) { m_rotation = radians; }
    void SetLocalScale(Vec2 scale)       { m_scale = scale; }

    void Translate(Vec2 delta)  { m_position += delta; }
    void Rotate(float radians)  { m_rotation += radians; }

    // ---- the tree ---------------------------------------------------------
    Transform2D*                     Parent()   const { return m_parent; }
    const std::vector<Transform2D*>& Children() const { return m_children; }

    // Attaches this transform to a new parent.
    //
    // `keepWorldTransform` decides what happens on screen. With it false, the
    // object keeps its local numbers and therefore jumps to wherever those
    // numbers mean under the new parent. With it true, the local numbers are
    // recomputed so the object does not appear to move at all - which is what
    // dragging something onto a new parent in the Hierarchy should do.
    void SetParent(Transform2D* parent, bool keepWorldTransform = false);

    // Hands every child back to the world, keeping them where they look.
    void DetachChildren();

    // How many parents there are above this one. A transform with no parent
    // has depth 0.
    int  Depth() const;
    bool IsDescendantOf(const Transform2D* candidate) const;

    // ---- matrices ---------------------------------------------------------
    // LocalMatrix turns this node's position/rotation/scale into a matrix.
    // WorldMatrix does the same but also folds in every parent above it.
    Mat3 LocalMatrix() const;
    Mat3 WorldMatrix() const;

    Vec2  WorldPosition() const;
    float WorldRotation() const;
    Vec2  WorldScale() const;

    // Moves the object to a world position, working out for you what local
    // position that corresponds to under the current parent.
    void SetWorldPosition(Vec2 world);

    // Converting a point or a direction between this object's own frame of
    // reference and the world's. A POINT is affected by the object's position;
    // a DIRECTION is not. See Mat3.h for why those are separate.
    Vec2 LocalToWorldPoint(Vec2 local) const;
    Vec2 WorldToLocalPoint(Vec2 world) const;
    Vec2 LocalToWorldVector(Vec2 local) const;
    Vec2 WorldToLocalVector(Vec2 world) const;

private:
    void AddChild(Transform2D* child);
    void RemoveChild(Transform2D* child);

    Vec2  m_position{0.0f, 0.0f};
    float m_rotation = 0.0f;
    Vec2  m_scale{1.0f, 1.0f};

    Transform2D*              m_parent = nullptr;
    std::vector<Transform2D*> m_children;
};

} // namespace eng


// ============================================================================
//  Overlap.h - "are these two shapes touching?", and nothing else.
//
//  Two shapes are supported, which is all a 2D game usually needs:
//    * AABB   - an Axis-Aligned Bounding Box. A rectangle that is never
//               rotated, described by its bottom-left and top-right corners.
//    * Circle - a centre and a radius.
//
//  Everything here is a PURE FUNCTION: it reads its arguments, returns an
//  answer, and changes nothing anywhere. The same inputs always give the same
//  result. That is what lets the collision system in physics/Collider.h build
//  triggers, layers and enter/exit events on top of these without ever needing
//  to change this file.
//
//  ==========================================================================
//  ONE DECISION, APPLIED EVERYWHERE: TOUCHING COUNTS AS OVERLAPPING.
//
//  Two boxes that share exactly one edge overlap. Two circles touching at one
//  point overlap. A point sitting exactly on a boundary is inside.
//
//  Neither answer is more "correct" than the other; what matters is picking
//  one and never wavering. This engine picked yes for two reasons: every
//  comparison below then reads as <= or >=, so there is only one operator to
//  keep straight, and a trigger that refuses to fire when the player is
//  exactly on its edge is a bug report waiting to happen.
//  ==========================================================================
// ============================================================================


namespace eng {

// A rectangle that is never rotated.
//
// It stores two corners rather than a centre and a size because the overlap
// test is then four straight comparisons with no arithmetic at all.
struct AABB {
    Vec2 min;   // bottom-left
    Vec2 max;   // top-right

    // Building a box from a centre point and its half-width/half-height, which
    // is how a collider on an entity is described.
    static constexpr AABB FromCenterHalfExtents(Vec2 center, Vec2 halfExtents) {
        return AABB{Vec2{center.x - halfExtents.x, center.y - halfExtents.y},
                    Vec2{center.x + halfExtents.x, center.y + halfExtents.y}};
    }

    static constexpr AABB FromMinMax(Vec2 lo, Vec2 hi) { return AABB{lo, hi}; }

    constexpr Vec2 Center() const {
        return Vec2{(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f};
    }
    constexpr Vec2 Size()    const { return Vec2{max.x - min.x, max.y - min.y}; }
    constexpr Vec2 Extents() const { return Vec2{Size().x * 0.5f, Size().y * 0.5f}; }

    // A box with min above max on either axis is inside-out and cannot contain
    // anything. Nothing forces this to be true - it is simply that every
    // function below reports "no overlap" for such a box rather than doing
    // something unpredictable.
    constexpr bool IsValid() const { return min.x <= max.x && min.y <= max.y; }

    // Grows the box just enough to include `point`. Used when working out the
    // upright box that surrounds a rotated shape.
    void Encapsulate(Vec2 point);
};

struct Circle {
    Vec2  center;
    float radius = 0.0f;
};

// The four shape pairings. Each has both argument orders so that calling code
// never has to remember which way round it was declared.
bool Overlaps(const AABB& a, const AABB& b);
bool Overlaps(const Circle& a, const Circle& b);
bool Overlaps(const AABB& box, const Circle& circle);
bool Overlaps(const Circle& circle, const AABB& box);

bool Contains(const AABB& box, Vec2 point);
bool Contains(const Circle& circle, Vec2 point);

// The point on (or inside) the box that is nearest to `point`.
//
// This is the function that makes box-versus-circle correct. The tempting
// shortcut is to put a box around the circle and compare the two boxes, but
// that reports a hit when a circle is near a box CORNER while still being too
// far away to touch it. Measuring to the closest point instead has no such
// blind spot.
Vec2 ClosestPointOnAABB(const AABB& box, Vec2 point);

} // namespace eng


// ============================================================================
//  Random.h - random numbers for gameplay.
//
//  WHY NOT rand()
//  The old C function rand() has one shared hidden state, a small range, and
//  quality that varies between compilers. C++ replaced it with the <random>
//  header, and that is what this class wraps.
//
//  WHY WRAP <random> AT ALL INSTEAD OF USING IT DIRECTLY
//  <random> is powerful but wordy: to get "a number between 1 and 6" you have
//  to create a generator, create a distribution, and then combine them. That
//  is worth learning eventually, and it is noise in the middle of gameplay
//  code. This class does it once, here, and gives you:
//
//      eng::Random dice;
//      int roll = dice.NextInt(1, 6);
//
//  WHY A SEED
//  A generator started from the same seed produces the same sequence of
//  numbers every time. That turns "it only crashes sometimes" into a bug you
//  can reproduce on demand, which is the difference between a fixable problem
//  and a haunted one. Each Random remembers its seed so it can be printed.
// ============================================================================


namespace eng {

class Random {
public:
    // An arbitrary fixed number, deliberately NOT the current time. A game
    // that seeds itself from the clock by default behaves differently on every
    // run, which makes bugs impossible to reproduce. Seeding from the clock is
    // something a caller should do on purpose and then log.
    static constexpr unsigned int kDefaultSeed = 12345u;

    Random() : Random(kDefaultSeed) {}
    explicit Random(unsigned int seed) : m_engine(seed), m_seed(seed) {}

    // A whole number from lo to hiInclusive, both ends possible.
    // NextInt(1, 6) is a six-sided die.
    int NextInt(int lo, int hiInclusive);

    // A decimal from 0 up to (but never exactly) 1.
    float NextFloat01();

    // A decimal somewhere between lo and hi.
    float NextRange(float lo, float hi);

    // A coin flip.
    bool NextBool();

    // A direction: a vector of length 1 pointing at a random angle. Handy for
    // scattering particles or picking a starting heading.
    struct UnitVector { float x, y; };
    UnitVector NextDirection();

    // Restarts the sequence from a new seed.
    void Reseed(unsigned int seed) { m_engine.seed(seed); m_seed = seed; }

    unsigned int Seed() const { return m_seed; }

private:
    // std::mt19937 is the Mersenne Twister, the standard library's general
    // purpose generator. It is the one to reach for unless you have a specific
    // reason not to: good statistical quality, well tested, and its sequence
    // is defined by the standard, so a given seed behaves the same everywhere.
    std::mt19937 m_engine;
    unsigned int m_seed = kDefaultSeed;
};

// One shared generator for code that just wants a random number and does not
// care about reproducing it. Anything that DOES care should make its own
// Random with its own seed.
//
// It is a function rather than a plain global variable on purpose: a global is
// created at an unpredictable moment during program start-up, whereas the
// variable inside this function is created the first time somebody calls it,
// which is by definition after everything it needs already exists.
Random& GlobalRandom();

} // namespace eng
