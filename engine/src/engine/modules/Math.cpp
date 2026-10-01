// ============================================================================
//  Math.cpp - vectors, matrices, transforms, shape overlap tests and random numbers.
//  See Math.h for what each piece is for.
// ============================================================================

#include <engine/modules/Diagnostics.h>
#include <engine/modules/Math.h>

#include <algorithm>
#include <cmath>
#include <utility>

// ============================================================================
//  Mat3.cpp - the 3x3 matrix maths declared in Mat3.h.
//
//  Everything here follows the convention written at the top of Mat3.h:
//  row-major storage, points written as rows, and `a * b` meaning "do a, then
//  b". If you change one, change the other in the same edit.
// ============================================================================



namespace eng {

Mat3 Mat3::Identity() {
    Mat3 result;
    result.m[0][0] = 1.0f; result.m[0][1] = 0.0f; result.m[0][2] = 0.0f;
    result.m[1][0] = 0.0f; result.m[1][1] = 1.0f; result.m[1][2] = 0.0f;
    result.m[2][0] = 0.0f; result.m[2][1] = 0.0f; result.m[2][2] = 1.0f;
    return result;
}

Mat3 Mat3::Translation(Vec2 t) {
    Mat3 result = Identity();
    // The BOTTOM ROW holds the move, because points are written as rows here.
    result.m[2][0] = t.x;
    result.m[2][1] = t.y;
    return result;
}

Mat3 Mat3::Rotation(float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);

    Mat3 result = Identity();
    //  [  c  s  0 ]   Multiplying the row (1, 0) by this gives (c, s), so a
    //  [ -s  c  0 ]   positive angle turns anticlockwise in a y-up world -
    //  [  0  0  1 ]   which is what the rest of the engine assumes.
    result.m[0][0] =  c; result.m[0][1] = s;
    result.m[1][0] = -s; result.m[1][1] = c;
    return result;
}

Mat3 Mat3::Scaling(Vec2 s) {
    Mat3 result = Identity();
    result.m[0][0] = s.x;
    result.m[1][1] = s.y;
    return result;
}

Mat3 Mat3::FromTRS(Vec2 translation, float radians, Vec2 scale) {
    // This is Scaling(scale) * Rotation(radians) * Translation(translation)
    // with the multiplication already worked out on paper. It is written
    // longhand because every object asks for its world matrix every frame, and
    // the straightforward version builds three whole matrices and multiplies
    // them just to fill in six numbers.
    const float c = std::cos(radians);
    const float s = std::sin(radians);

    Mat3 result;
    result.m[0][0] = scale.x *  c;  result.m[0][1] = scale.x * s;   result.m[0][2] = 0.0f;
    result.m[1][0] = scale.y * -s;  result.m[1][1] = scale.y * c;   result.m[1][2] = 0.0f;
    result.m[2][0] = translation.x; result.m[2][1] = translation.y; result.m[2][2] = 1.0f;
    return result;
}

Vec2 Mat3::TransformPoint(Vec2 point) const {
    // The row [x y 1] multiplied by this matrix, keeping the first two results.
    // The 1 on the end is what picks up the bottom row, which is exactly why
    // the move applies to a point.
    return Vec2{point.x * m[0][0] + point.y * m[1][0] + m[2][0],
                point.x * m[0][1] + point.y * m[1][1] + m[2][1]};
}

Vec2 Mat3::TransformVector(Vec2 direction) const {
    // The row [x y 0]. The 0 is the whole difference from TransformPoint: it
    // multiplies the bottom row away, so the move is ignored and only the
    // turn and the resize apply.
    return Vec2{direction.x * m[0][0] + direction.y * m[1][0],
                direction.x * m[0][1] + direction.y * m[1][1]};
}

Mat3 Mat3::Inverse() const {
    // Every matrix this engine builds has (0, 0, 1) as its third column - that
    // is what "made only of moves, turns and resizes" means. Knowing that
    // turns a full 3x3 inverse into a small 2x2 one plus an adjusted offset.
    const float a = m[0][0];
    const float b = m[0][1];
    const float c = m[1][0];
    const float d = m[1][1];

    // The determinant of the 2x2 part. When it is zero the matrix squashes the
    // world flat onto a line, and there is no way to un-squash it.
    const float determinant = a * d - b * c;
    if (std::fabs(determinant) < 1e-12f) {
        ENGINE_LOG_WARN(Channels::kCore,
                        "Mat3::Inverse called on a matrix that cannot be undone "
                        "(is something scaled to zero?); returning identity");
        return Identity();
    }

    const float invDet = 1.0f / determinant;

    Mat3 result = Identity();
    result.m[0][0] =  d * invDet;
    result.m[0][1] = -b * invDet;
    result.m[1][0] = -c * invDet;
    result.m[1][1] =  a * invDet;

    // Undoing the move as well: the offset has to be pushed back through the
    // inverted rotate/scale part and negated.
    const float tx = m[2][0];
    const float ty = m[2][1];
    result.m[2][0] = -(tx * result.m[0][0] + ty * result.m[1][0]);
    result.m[2][1] = -(tx * result.m[0][1] + ty * result.m[1][1]);

    return result;
}

Vec2 Mat3::GetTranslation() const {
    return Vec2{m[2][0], m[2][1]};
}

Vec2 Mat3::GetScale() const {
    // How long each of the first two rows is. A matrix that also mirrors
    // (a negative scale) reports a positive number here; that limitation does
    // not matter for what this is used for - sprite sizes and Inspector boxes.
    const Vec2 rowX{m[0][0], m[0][1]};
    const Vec2 rowY{m[1][0], m[1][1]};
    return Vec2{rowX.Length(), rowY.Length()};
}

float Mat3::GetRotation() const {
    // Row 0 is (scaleX * cos, scaleX * sin), and atan2 only cares about the
    // ratio of its two arguments, so a positive scale cancels out.
    return std::atan2(m[0][1], m[0][0]);
}

Mat3 operator*(const Mat3& a, const Mat3& b) {
    // Ordinary matrix multiplication: each output element is one row of `a`
    // multiplied through one column of `b`.
    Mat3 result;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            result.m[row][col] = a.m[row][0] * b.m[0][col] +
                                 a.m[row][1] * b.m[1][col] +
                                 a.m[row][2] * b.m[2][col];
        }
    }
    return result;
}

bool ApproxEqual(const Mat3& a, const Mat3& b, float epsilon) {
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            if (!ApproxEqual(a.m[row][col], b.m[row][col], epsilon)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace eng


// ============================================================================
//  Transform2D.cpp - the parent/child transform tree declared in Transform2D.h.
//
//  The two rules from the header ("children survive their parent" and "nothing
//  can be its own ancestor") are enforced in ~Transform2D and SetParent
//  respectively.
// ============================================================================



namespace eng {

Transform2D::~Transform2D() {
    // Rule 1: hand the children back to the world rather than destroying them
    // or leaving them pointing at memory that is about to disappear.
    DetachChildren();

    // And take this node off its own parent's child list, so the parent is not
    // left holding a pointer to something that no longer exists.
    if (m_parent != nullptr) {
        m_parent->RemoveChild(this);
        m_parent = nullptr;
    }
}

void Transform2D::AddChild(Transform2D* child) {
    m_children.push_back(child);
}

void Transform2D::RemoveChild(Transform2D* child) {
    // std::erase removes every matching element from a container, and does
    // nothing when there is no match - so there is no "did we find it?" step to
    // get wrong. A child appears in this list once, so "every match" is one.
    std::erase(m_children, child);
}

void Transform2D::SetParent(Transform2D* parent, bool keepWorldTransform) {
    if (parent == m_parent) {
        return;   // nothing to do
    }

    // Rule 2: refuse to build a loop. Walking up the proposed parent's chain
    // costs almost nothing, and it turns a permanent freeze inside the
    // renderer into a message in the Console at the moment the mistake is made.
    if (parent != nullptr) {
        if (parent == this || parent->IsDescendantOf(this)) {
            ENGINE_LOG_ERROR(Channels::kScene,
                             "refused to reparent a transform under itself or one of "
                             "its own children - that would make a loop");
            return;
        }
    }

    // Remember where the object currently looks, before the parent changes.
    const Mat3 worldBefore = keepWorldTransform ? WorldMatrix() : Mat3::Identity();

    if (m_parent != nullptr) {
        m_parent->RemoveChild(this);
    }
    m_parent = parent;
    if (m_parent != nullptr) {
        m_parent->AddChild(this);
    }

    if (keepWorldTransform) {
        // Work out what local position/rotation/scale would put the object
        // back exactly where it was: take the world transform it had, and undo
        // the new parent's transform from it.
        const Mat3 parentWorld = (m_parent != nullptr) ? m_parent->WorldMatrix()
                                                       : Mat3::Identity();
        const Mat3 local = worldBefore * parentWorld.Inverse();
        m_position = local.GetTranslation();
        m_rotation = local.GetRotation();
        m_scale    = local.GetScale();
    }
}

void Transform2D::DetachChildren() {
    // The list is COPIED before it is walked. SetParent below calls
    // RemoveChild on this node, which erases from m_children - and modifying a
    // container while looping over it is how you end up reading freed memory.
    // Iterating a copy sidesteps that completely.
    const std::vector<Transform2D*> children = m_children;
    for (Transform2D* child : children) {
        child->SetParent(nullptr, /*keepWorldTransform=*/true);
    }
    m_children.clear();
}

int Transform2D::Depth() const {
    int depth = 0;
    for (const Transform2D* node = m_parent; node != nullptr; node = node->m_parent) {
        ++depth;
    }
    return depth;
}

bool Transform2D::IsDescendantOf(const Transform2D* candidate) const {
    if (candidate == nullptr) {
        return false;
    }
    for (const Transform2D* node = m_parent; node != nullptr; node = node->m_parent) {
        if (node == candidate) {
            return true;
        }
    }
    return false;
}

Mat3 Transform2D::LocalMatrix() const {
    return Mat3::FromTRS(m_position, m_rotation, m_scale);
}

Mat3 Transform2D::WorldMatrix() const {
    // Start with this node's own transform, then apply each parent in turn
    // going outward. Under this engine's convention (see Mat3.h) "do local,
    // then the parent" is written local * parent, which is the same order it
    // reads in.
    //
    // This walks the whole chain every time it is called rather than caching
    // the answer. That is deliberate: caching means remembering to invalidate
    // the cache every time anything moves, and a stale transform is a much
    // harder bug than a slightly slower one. Scenes here are small enough that
    // it does not matter.
    Mat3 result = LocalMatrix();
    for (const Transform2D* node = m_parent; node != nullptr; node = node->m_parent) {
        result = result * node->LocalMatrix();
    }
    return result;
}

Vec2 Transform2D::WorldPosition() const {
    return WorldMatrix().GetTranslation();
}

float Transform2D::WorldRotation() const {
    // Rotations simply add up the chain, so there is no need to build a matrix
    // and pull the angle back out of it.
    float total = m_rotation;
    for (const Transform2D* node = m_parent; node != nullptr; node = node->m_parent) {
        total += node->m_rotation;
    }
    return total;
}

Vec2 Transform2D::WorldScale() const {
    return WorldMatrix().GetScale();
}

void Transform2D::SetWorldPosition(Vec2 world) {
    if (m_parent == nullptr) {
        // With no parent, local and world are the same thing.
        m_position = world;
        return;
    }
    // With a parent, undo the parent's transform to find the local position
    // that lands on the requested world position.
    m_position = m_parent->WorldMatrix().Inverse().TransformPoint(world);
}

Vec2 Transform2D::LocalToWorldPoint(Vec2 local) const {
    return WorldMatrix().TransformPoint(local);
}

Vec2 Transform2D::WorldToLocalPoint(Vec2 world) const {
    return WorldMatrix().Inverse().TransformPoint(world);
}

Vec2 Transform2D::LocalToWorldVector(Vec2 local) const {
    return WorldMatrix().TransformVector(local);
}

Vec2 Transform2D::WorldToLocalVector(Vec2 world) const {
    return WorldMatrix().Inverse().TransformVector(world);
}

} // namespace eng


// ============================================================================
//  Overlap.cpp - the shape tests declared in Overlap.h.
//
//  Every comparison below uses <= or >= rather than < or >, because this
//  engine has decided that touching counts as overlapping. If you ever change
//  that, change all of them together and update the note in the header.
//
//  <algorithm> is included for std::min, std::max and std::clamp - all three
//  are standard-library functions, and writing them by hand with an if/else
//  only creates somewhere for a typo to hide.
// ============================================================================



namespace eng {

void AABB::Encapsulate(Vec2 point) {
    min.x = std::min(min.x, point.x);
    min.y = std::min(min.y, point.y);
    max.x = std::max(max.x, point.x);
    max.y = std::max(max.y, point.y);
}

bool Overlaps(const AABB& a, const AABB& b) {
    // Two upright rectangles miss each other if there is a gap on EITHER axis.
    // So instead of proving they overlap, prove they cannot: if one box ends
    // before the other begins on x, or on y, there is no contact.
    if (a.max.x < b.min.x || b.max.x < a.min.x) { return false; }
    if (a.max.y < b.min.y || b.max.y < a.min.y) { return false; }
    return true;
}

bool Overlaps(const Circle& a, const Circle& b) {
    // Two circles touch when the distance between their centres is no more
    // than the sum of their radii.
    const float reach = a.radius + b.radius;

    // Both sides are squared so that no square root is needed. Squaring does
    // not change which of two non-negative numbers is larger, so the answer is
    // identical and the work is less.
    return DistanceSquared(a.center, b.center) <= reach * reach;
}

Vec2 ClosestPointOnAABB(const AABB& box, Vec2 point) {
    // std::clamp(v, lo, hi) returns v pinned into the range [lo, hi]. Doing
    // that on each axis independently is exactly "the nearest point in the
    // rectangle", and it needs no branches or special cases.
    return Vec2{std::clamp(point.x, box.min.x, box.max.x),
                std::clamp(point.y, box.min.y, box.max.y)};
}

bool Overlaps(const AABB& box, const Circle& circle) {
    // Find the point of the box nearest the circle's centre, then ask whether
    // that point is within one radius.
    //
    // This one expression handles all three situations without any branching:
    //   * the centre is off one flat side  -> nearest point is on that side
    //   * the centre is off a corner       -> nearest point is the corner
    //   * the centre is inside the box     -> nearest point IS the centre, so
    //                                         the distance is zero and it
    //                                         always counts as an overlap
    const Vec2 closest = ClosestPointOnAABB(box, circle.center);
    return DistanceSquared(closest, circle.center) <= circle.radius * circle.radius;
}

bool Overlaps(const Circle& circle, const AABB& box) {
    // The same question with the arguments swapped, so calling code never has
    // to remember an order.
    return Overlaps(box, circle);
}

bool Contains(const AABB& box, Vec2 point) {
    return point.x >= box.min.x && point.x <= box.max.x &&
           point.y >= box.min.y && point.y <= box.max.y;
}

bool Contains(const Circle& circle, Vec2 point) {
    return DistanceSquared(circle.center, point) <= circle.radius * circle.radius;
}

} // namespace eng


// ============================================================================
//  Random.cpp - the random number helpers declared in Random.h.
//
//  Each function pairs the generator (m_engine, which produces raw random
//  bits) with a DISTRIBUTION (which reshapes those bits into the range and
//  spread you asked for). That two-part split is how <random> is designed:
//  generators and distributions are separate so either can be swapped without
//  touching the other.
// ============================================================================



namespace eng {

int Random::NextInt(int lo, int hiInclusive) {
    if (lo > hiInclusive) {
        // Rather than return something arbitrary, say so - a reversed range is
        // almost always a typo in the calling code - and then carry on with
        // the range the caller probably meant.
        ENGINE_LOG_WARN(Channels::kCore,
                        "Random::NextInt called with lo={} greater than hi={}; "
                        "swapping them",
                        lo, hiInclusive);
        std::swap(lo, hiInclusive);
    }

    // std::uniform_int_distribution gives every value in the range an equal
    // chance. Writing `m_engine() % range` by hand instead is very slightly
    // biased towards the low numbers and is the classic beginner mistake here.
    std::uniform_int_distribution<int> distribution(lo, hiInclusive);
    return distribution(m_engine);
}

float Random::NextFloat01() {
    // The range is written as [0, 1) - 0 is possible, 1 is not. That is the
    // convention every random-float API uses, and it is what makes
    // `array[(int)(NextFloat01() * size)]` safe.
    std::uniform_real_distribution<float> distribution(0.0f, 1.0f);
    return distribution(m_engine);
}

float Random::NextRange(float lo, float hi) {
    if (lo > hi) {
        std::swap(lo, hi);
    }
    std::uniform_real_distribution<float> distribution(lo, hi);
    return distribution(m_engine);
}

bool Random::NextBool() {
    // std::bernoulli_distribution is the standard "true with probability p"
    // distribution; 0.5 makes it a fair coin.
    std::bernoulli_distribution distribution(0.5);
    return distribution(m_engine);
}

Random::UnitVector Random::NextDirection() {
    // Pick an angle anywhere around the circle, then convert it to x and y.
    // The result always has length 1, which is what "a direction" means.
    const float angle = NextRange(0.0f, kTwoPi);
    return UnitVector{std::cos(angle), std::sin(angle)};
}

Random& GlobalRandom() {
    // A "function-local static": created the first time this function runs,
    // then reused forever after. See the note in Random.h for why this is
    // preferred over a plain global variable.
    static Random instance;
    return instance;
}

} // namespace eng
