#pragma once

// =============================================================================
//  WEEK 9 - components.
//
//  A component is data or behaviour attached to an entity, and it REGISTERS
//  ITSELF with the system that updates it.
//
//  ---------------------------------------------------------------------------
//  "REGISTERS ITSELF" - being precise about it, because it is the whole point.
//
//  The naive design has one central update that walks every entity and asks
//  each what it is. That is a giant switch, it pointer-chases in the worst
//  possible pattern, and every new component type means editing a shared file.
//
//  Instead: when a SpriteComponent is attached, it appends a SpriteRecord to
//  the render system's array. When it is detached, it removes it. The render
//  system iterates a dense, contiguous array of exactly the fields it cares
//  about, and never asks anything what type it is.
//
//  WHAT WEEK 4's MEASUREMENT SAID, AND WHETHER THIS FOLLOWED IT.
//
//  The Week 4 report measured SoA about 2.4x faster than AoS at 100,000
//  particles, and roughly break-even below about 3,000 - the crossover being
//  where the working set stops fitting in L2. Phase 1 scenes are 22 entities
//  and Phase 2 will be hundreds, which is FAR below that crossover.
//
//  So this is a middle option, chosen on the evidence rather than on the
//  headline: the render system stores a compact RECORD (transform pointer,
//  texture handle, tint, layer) in one contiguous vector, rather than either
//  a vector of pointers to fat component objects (which would chase a pointer
//  per sprite into scattered heap allocations - strictly worse than AoS) or
//  full SoA (whose benefit does not materialise at these counts and whose cost
//  in readability is real). Splitting these into parallel arrays is a
//  mechanical change if Phase 2 profiling ever asks for it.
//
//  ---------------------------------------------------------------------------
//  REQUIRED THIS WEEK: TransformComponent and SpriteComponent. Two is enough
//  to prove the model. Week 10 adds colliders, and they went in
//  physics/Collider.h rather than here, so the two files do not grow together.
//
//  COMPONENTS ARE CONSTRUCTED FROM DATA. Every parameter in a scene file
//  reaches the component through Deserialize without a C++ change. If adding
//  a new sprite to a scene needed C++, the model would not be finished.
//
//  ---------------------------------------------------------------------------
//  REGISTRATION HAPPENS IN OnAttach/OnDetach, NOT IN THE CONSTRUCTOR AND
//  DESTRUCTOR. During construction the object is not yet fully formed - the
//  owner pointer is not set and the derived part may not exist - and handing a
//  pointer to a half-built object to a system that might immediately use it is
//  a real hazard. Ch. 6.1's start-up ordering, one scale down.
// =============================================================================

#include <engine/core/Config.h>
#include <engine/core/StringId.h>
#include <engine/math/Transform2D.h>
#include <engine/platform/Renderer.h>
#include <engine/resource/Handle.h>
#include <engine/resource/ResourceManager.h>
#include <engine/scene/Entity.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace eng {

class Entity;
class Scene;

class Component {
public:
    virtual ~Component() = default;

    // The type name as a StringId. This is what a scene file writes and what a
    // C# layer will eventually ask for - which is why identity is a runtime
    // NAME rather than a C++ type known only at compile time.
    virtual StringId    TypeId() const = 0;
    virtual const char* TypeName() const = 0;

    // Built from a data file. Returns false and fills outError with the ENTITY
    // AND FIELD that failed - "Asteroid07.SpriteComponent.texture is not a
    // string", not "parse error".
    virtual bool Deserialize(const ConfigNode& node, std::string& outError) = 0;

    // The counterpart, and the thing that turns the Inspector from a viewer
    // into an editor. Writes this component's fields as the same keys
    // Deserialize reads, so `load -> edit -> save -> load` is a fixed point.
    //
    // Returns FALSE for a component that does not support saving, and the
    // default does exactly that. Scene::Save then WARNS, naming the type it
    // could not write, rather than silently producing a file that has quietly
    // dropped a component. A save that loses data without saying so is worse
    // than a save that refuses.
    virtual bool Serialize(ConfigWriter& out) const {
        (void)out;
        return false;
    }

    virtual void OnAttach() {}
    virtual void OnDetach() {}

    Entity*       Owner() const { return m_owner; }
    EntityHandle  OwnerHandle() const;
    Scene*        GetScene() const;

    // Convenience: almost every component wants its entity's transform.
    Transform2D* OwnerTransform() const;

private:
    friend class Entity;
    Entity* m_owner = nullptr;
};

// ---------------------------------------------------------------------------
//  The factory. Something has to turn the string "SpriteComponent" from a file
//  into an object, and that is a table from StringId to a creation function.
//
//  Twenty lines, and deliberately NOT a reflection system - Week 9 has neither
//  the time nor the need, and a half-built reflection system is worse than
//  none.
// ---------------------------------------------------------------------------
class ComponentFactory {
public:
    using CreateFn = std::unique_ptr<Component> (*)();

    static void Register(std::string_view typeName, CreateFn create);
    static std::unique_ptr<Component> Create(StringId typeId);
    static std::unique_ptr<Component> Create(std::string_view typeName);
    static bool IsRegistered(StringId typeId);
    static void ForEachType(const std::function<void(const char*)>& fn);

    // Registers every built-in component type. Called once by the scene
    // subsystem at boot rather than by static constructors, so that the
    // registration ORDER is something written down rather than a property of
    // the link line - which is the static initialization order fiasco again.
    static void RegisterBuiltins();
};

// ---------------------------------------------------------------------------
//  TransformComponent
// ---------------------------------------------------------------------------
class TransformComponent final : public Component {
public:
    static constexpr const char* kTypeName = "TransformComponent";
    static StringId TypeIdStatic();

    StringId    TypeId() const override { return TypeIdStatic(); }
    const char* TypeName() const override { return kTypeName; }

    bool Deserialize(const ConfigNode& node, std::string& outError) override;
    bool Serialize(ConfigWriter& out) const override;

    Transform2D&       Transform()       { return m_transform; }
    const Transform2D& Transform() const { return m_transform; }

private:
    Transform2D m_transform;
};

// ---------------------------------------------------------------------------
//  SpriteComponent and the render system it registers with
// ---------------------------------------------------------------------------
class SpriteComponent;

// The dense record the render system actually walks. See the AoS/SoA note at
// the top of this file for why this shape rather than a vector of component
// pointers.
struct SpriteRecord {
    Transform2D*    transform = nullptr;
    Handle<Texture> texture{};
    Color           tint{};
    i32             layer = 0;
    SpriteComponent* owner = nullptr;   // back-pointer, for swap-and-pop fixup
};

class SpriteRenderSystem {
public:
    static void Register(SpriteComponent& sprite);
    static void Unregister(SpriteComponent& sprite);

    // Draws every registered sprite, back layer first. Called from the render
    // stage of the Week 10 system order.
    static void Render(class Camera& camera);

    static usize Count();
    static void  Clear();
};

class SpriteComponent final : public Component {
public:
    static constexpr const char* kTypeName = "SpriteComponent";
    static StringId TypeIdStatic();

    ~SpriteComponent() override;

    StringId    TypeId() const override { return TypeIdStatic(); }
    const char* TypeName() const override { return kTypeName; }

    bool Deserialize(const ConfigNode& node, std::string& outError) override;
    bool Serialize(ConfigWriter& out) const override;

    void OnAttach() override;
    void OnDetach() override;

    Handle<Texture>    GetTexture() const { return m_texture; }
    const std::string& TexturePath() const { return m_texturePath; }
    Color              Tint() const { return m_tint; }
    i32                Layer() const { return m_layer; }
    Vec2               PixelSize() const { return m_pixelSize; }

    void SetTint(Color tint);
    void SetLayer(i32 layer);
    // Releases the old texture and acquires the new one, keeping the refcount
    // rule (one Acquire, one Release, both pointable-at) intact.
    void SetTexture(std::string_view virtualPath);

private:
    friend class SpriteRenderSystem;

    Handle<Texture> m_texture{};
    std::string     m_texturePath;
    Color           m_tint = Color::White();
    i32             m_layer = 0;
    Vec2            m_pixelSize{0.0f, 0.0f};   // 0,0 means "use the texture's"
    usize           m_recordIndex = static_cast<usize>(-1);
};

} // namespace eng
