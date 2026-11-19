// ============================================================================
//  CameraComponent.cpp - the game camera as an entity. See the header for why.
// ============================================================================

#include <engine/Engine.h>
#include <engine/core/Log.h>
#include <engine/scene/CameraComponent.h>
#include <engine/scene/Entity.h>

#include <algorithm>
#include <vector>

namespace eng {
namespace {

std::vector<CameraComponent*> g_cameras;

// Remembered so that the FIRST frame after the active camera changes can say
// so in the log. A scene with two cameras silently switching between them is
// exactly the kind of thing that is hard to work out from the picture alone.
const CameraComponent* g_lastActive = nullptr;

} // namespace

CameraComponent::~CameraComponent() = default;

void CameraComponent::SetZoom(float zoom) {
    // A zoom of zero or less would divide by zero in every world-to-screen
    // conversion in the engine. Clamped rather than refused, because the
    // Inspector drags this and dragging through zero is normal.
    m_zoom = std::max(0.01f, zoom);
}

AABB CameraComponent::WorldBounds(Vec2 viewportPixels) const {
    const Transform2D* transform = OwnerTransform();
    const Vec2         centre    = (transform != nullptr) ? transform->WorldPosition()
                                                          : Vec2{0.0f, 0.0f};

    // The viewport is in PIXELS and the zoom says how many of them one world
    // unit covers, so the world area shown is the viewport divided by it.
    const float zoom = std::max(0.01f, m_zoom);
    const Vec2  half{viewportPixels.x * 0.5f / zoom, viewportPixels.y * 0.5f / zoom};

    return AABB::FromCenterHalfExtents(centre, half);
}

bool CameraComponent::Deserialize(const Json& node, std::string& outError) {
    SetZoom(ReadFloat(node, "zoom", 1.0f, kTypeName));

    // ==================================================================
    //  "frameHeight" WINS WHEN PRESENT, and is absent from every camera
    //  written before it existed - so a component with only a zoom reads
    //  exactly as it always did, default included.
    // ==================================================================
    const float height = ReadFloat(node, "frameHeight", 0.0f, kTypeName);
    if (height > 0.0f) {
        SetFrameHeight(height);
    } else {
        m_frameHeight = 0.0f;
    }

    m_priority = ReadInt(node, "priority", 0, kTypeName);
    m_enabled  = ReadBool(node, "enabled", true, kTypeName);

    outError.clear();
    return true;
}

bool CameraComponent::Serialize(Json& out) const {
    // EXACTLY ONE OF THE TWO, for the reason Scene's own camera block gives:
    // a camera is framed one way or the other, and writing both would make a
    // round trip depend on which key the reader preferred.
    if (m_frameHeight > 0.0f) {
        out["frameHeight"] = m_frameHeight;
    } else {
        out["zoom"] = m_zoom;
    }
    out["priority"] = m_priority;
    out["enabled"]  = m_enabled;
    return true;
}

void CameraComponent::SetFrameHeight(float worldUnits) {
    // Clamped like SetZoom, and for the same reason: a height of zero is a
    // zoom of infinity, and the view matrix it produces cannot be inverted -
    // so nothing could be clicked on.
    m_frameHeight = std::max(0.0f, worldUnits);
}

void CameraComponent::OnAttach() {
    CameraSystem::Register(*this);

    // Applied straight away so that dropping a camera into a scene in the
    // editor moves the Game view there immediately, rather than on whatever
    // frame something else happens to trigger a sync.
    CameraSystem::Apply();
}

void CameraComponent::OnDetach() {
    CameraSystem::Unregister(*this);
}

// ---------------------------------------------------------------------------
//  CameraSystem
// ---------------------------------------------------------------------------

void CameraSystem::Register(CameraComponent& camera) {
    g_cameras.push_back(&camera);
}

void CameraSystem::Unregister(CameraComponent& camera) {
    std::erase(g_cameras, &camera);

    if (g_lastActive == &camera) {
        g_lastActive = nullptr;
    }
}

std::size_t CameraSystem::Count() {
    return g_cameras.size();
}

void CameraSystem::Clear() {
    g_cameras.clear();
    g_lastActive = nullptr;
}

CameraComponent* CameraSystem::Active() {
    CameraComponent* best = nullptr;

    for (CameraComponent* camera : g_cameras) {
        if (camera == nullptr || !camera->Enabled()) {
            continue;
        }
        // A camera on a switched-off entity cannot be the active one. Without
        // this, switching off a whole menu or cut-scene subtree would leave
        // its camera still framing the shot. See Entity::IsActiveInHierarchy.
        if (!camera->OwnerIsActive()) {
            continue;
        }
        // STRICTLY GREATER, so a tie goes to whichever was found first rather
        // than to whichever happens to be last in the list. Two cameras with
        // the same priority should at least pick the same one every frame.
        if (best == nullptr || camera->Priority() > best->Priority()) {
            best = camera;
        }
    }
    return best;
}

void CameraSystem::Apply() {
    CameraComponent* active = Active();

    // NOTHING AT ALL when there is no camera entity. The engine's camera
    // keeps whatever the scene file's "camera" block gave it, which is
    // exactly how every scene written before this existed behaves. See the
    // header.
    if (active == nullptr) {
        if (g_lastActive != nullptr) {
            ENGINE_LOG_INFO(Channels::kScene,
                            "no camera entity is active; the view is back to the "
                            "scene's own camera setting");
            g_lastActive = nullptr;
        }
        return;
    }

    if (g_lastActive != active) {
        const Entity* owner = active->Owner();
        ENGINE_LOG_INFO(Channels::kScene, "the view is now driven by '{}'",
                        owner != nullptr ? owner->Name() : "a camera");
        g_lastActive = active;
    }

    const Transform2D* transform = active->OwnerTransform();
    if (transform == nullptr) {
        return;
    }

    // WORLD position, so a camera parented to a vehicle rides along with it -
    // which is most of the reason for making the camera an entity at all.
    Camera& view = Engine::Get().GetCamera();
    view.SetPosition(transform->WorldPosition());
    // WHICHEVER WAY THIS CAMERA IS FRAMED. SetFrameHeight derives the zoom
    // from the viewport, so the amount of world in shot stops depending on
    // the window - see render/Camera.h.
    if (active->FrameHeight() > 0.0f) {
        view.SetFrameHeight(active->FrameHeight());
    } else {
        view.SetZoom(active->Zoom());
    }
}

void CameraSystem::Update(float /*deltaSeconds*/) {
    Apply();
}

} // namespace eng
