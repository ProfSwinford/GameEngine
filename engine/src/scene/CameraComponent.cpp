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

// Set when the camera that WAS driving the view is unregistered, because the
// pointer above has to be cleared at that moment and clearing it alone loses
// the transition Apply's hand-back branch keys on. See Unregister.
bool g_handBackIsOwed = false;

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

    // ======================================================================
    //  THE POINTER GOES, BUT THE FACT THAT THERE WAS ONE IS REMEMBERED.
    //
    //  The pointer HAS to be cleared here: this runs from OnDetach, so the
    //  object is about to be destroyed and nothing may compare against it
    //  afterwards.
    //
    //  CLEARING IT ALONE LOST THE TRANSITION. Apply's hand-back branch is
    //  reached only when g_lastActive says a camera was driving the view, so
    //  deleting the active camera entity changed neither the picture nor the
    //  Console: the view silently kept the deleted camera's shot, and the
    //  next save wrote that shot over the scene's own camera block.
    // ======================================================================
    if (g_lastActive == &camera) {
        g_lastActive     = nullptr;
        g_handBackIsOwed = true;
    }
}

std::size_t CameraSystem::Count() {
    return g_cameras.size();
}

void CameraSystem::Clear() {
    g_cameras.clear();
    g_lastActive     = nullptr;

    // NOT OWED ACROSS A Clear. Clear is the end of a scene, and the next
    // scene's own load applies its own camera block - so a hand-back left
    // owed here would fire against a scene that had already framed itself.
    g_handBackIsOwed = false;
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

    // ======================================================================
    //  NO CAMERA ENTITY: THE VIEW GOES BACK TO THE SCENE'S OWN SETTING.
    //
    //  And it now really does. This branch used to say so in the Console and
    //  do nothing, which was wrong twice over:
    //
    //    THE PICTURE. Unticking or deleting the last camera left the view
    //    showing that camera's last shot for the rest of the session. Only a
    //    scene load or leaving play mode ever applied the scene's own block.
    //
    //    THE FILE. Engine::SaveScene declines to write the block only while a
    //    camera IS active. With no active camera the gate opens - and what it
    //    then wrote out was the dead camera's derived pose, straight over the
    //    hand-authored block. That is the precise overwrite the block exists
    //    to survive, and it was reachable by unticking a camera and pressing
    //    Ctrl+S. Worse, a block authored as a frame height came back as a
    //    zoom, because the camera had been zoom-framed by the entity.
    //
    //  ONCE, ON THE TRANSITION, not every frame: re-applying continuously
    //  would fight anything that deliberately moves the engine camera while
    //  no entity drives it - which is what a scene with no camera entity at
    //  all does, and is what the Game view's camera fields are for.
    // ======================================================================
    if (active == nullptr) {
        if (g_lastActive != nullptr || g_handBackIsOwed) {
            ENGINE_LOG_INFO(Channels::kScene,
                            "no camera entity is active; the view is back to the "
                            "scene's own camera setting");
            g_lastActive     = nullptr;
            g_handBackIsOwed = false;
            Engine::Get().ApplySceneCamera();
        }
        return;
    }

    // A camera has taken over again, so nothing is owed to the scene's block.
    g_handBackIsOwed = false;

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
