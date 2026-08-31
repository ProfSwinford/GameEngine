#pragma once

// ============================================================================
//  Component.h - the base class for everything an entity can have, plus the
//  two components every game needs: Transform and Sprite.
//
//  A component is a piece of data or behaviour attached to an entity. It also
//  REGISTERS ITSELF with whichever system updates or draws it.
//
//  WHY "REGISTERS ITSELF" MATTERS
//  The straightforward design has one big update function that walks every
//  entity and asks each one what it is. That means a giant if/else chain, and
//  it means editing a shared file every time somebody adds a component type.
//
//  Instead: when a SpriteComponent is attached, it adds an entry to the render
//  system's list. When it is detached, it takes that entry back out. The
//  render system then walks its own list of exactly the things it cares about
//  and never asks anything what type it is.
//
//  COMPONENTS ARE BUILT FROM DATA
//  Every setting a component has reaches it through Deserialize, reading the
//  scene file. Adding another sprite to a level is an edit to a .json file,
//  not to C++. If it were not, the whole component model would be pointless.
//
//  REGISTRATION HAPPENS IN OnAttach/OnDetach, NOT IN THE CONSTRUCTOR
//  While a constructor is running, the object is not finished: its owner is
//  not set yet and the derived part may not exist. Handing a pointer to a
//  half-built object to a system that might use it immediately is a real
//  hazard. OnAttach is called once everything is in place.
// ============================================================================

#include <engine/core/Json.h>
#include <engine/math/Transform2D.h>
#include <engine/render/Renderer.h>
#include <engine/render/Texture.h>
#include <engine/scene/Entity.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace eng {

class Camera;
class Entity;
class Scene;

class Component {
public:
    virtual ~Component() = default;

    // The name this component is known by, in the scene file and in the
    // editor - "SpriteComponent", "BoxColliderComponent". Identity is a NAME
    // rather than a C++ type because a scene file can only contain text.
    virtual const char* TypeName() const = 0;

    // Fills this component in from one entry in a scene file.
    //
    // Returns false and puts an explanation in outError. That message should
    // name the FIELD - "SpriteComponent.texture must be text", not "error" -
    // because the person reading it has a file open and needs to know which
    // line to fix.
    virtual bool Deserialize(const Json& node, std::string& outError) = 0;

    // The opposite: writes this component back out using the same keys
    // Deserialize reads. This is what turns the Inspector from a viewer into
    // an editor, because it is how an edit survives being saved.
    //
    // The default returns false, meaning "this type cannot be saved". Saving a
    // scene containing one of those warns and names the type rather than
    // quietly writing a file with a component missing. A save that loses work
    // without saying so is worse than one that refuses.
    virtual bool Serialize(Json& out) const {
        (void)out;   // silences "unused parameter" without naming it
        return false;
    }

    // Called once, when the component has been fully attached to its entity,
    // and once when it is being taken off. Registering with systems goes here.
    virtual void OnAttach() {}
    virtual void OnDetach() {}

    Entity*      Owner() const { return m_owner; }
    EntityId     OwnerId() const;
    Scene*       GetScene() const;

    // Nearly every component wants its entity's transform, so here it is.
    Transform2D* OwnerTransform() const;

private:
    friend class Entity;
    Entity* m_owner = nullptr;
};

// ---------------------------------------------------------------------------
//  ComponentFactory - turns the text "SpriteComponent" into a real object.
//
//  Something has to bridge between a name in a file and a C++ class, and this
//  is it: a table from type name to a function that makes one.
// ---------------------------------------------------------------------------
class ComponentFactory {
public:
    // A plain function pointer that makes one component. `using` gives that
    // otherwise unreadable type a name.
    using CreateFn = std::unique_ptr<Component> (*)();

    static void Register(std::string_view typeName, CreateFn create);
    static std::unique_ptr<Component> Create(std::string_view typeName);
    static bool IsRegistered(std::string_view typeName);

    // Lists every registered type, which is how the Inspector's "Add
    // Component" menu is built without anybody typing the list out twice.
    static void ForEachType(const std::function<void(const char*)>& fn);

    // Registers the component types the engine ships with. Called once at
    // start-up from a known place, rather than by scattered global objects, so
    // that the order things register in is something written down instead of
    // an accident of how the files were linked.
    static void RegisterBuiltins();
};

// ---------------------------------------------------------------------------
//  TransformComponent - where the entity is.
//
//  Scene file fields:  "position": [x, y], "rotation": radians, "scale": [x, y]
// ---------------------------------------------------------------------------
class TransformComponent final : public Component {
public:
    static constexpr const char* kTypeName = "TransformComponent";

    const char* TypeName() const override { return kTypeName; }

    bool Deserialize(const Json& node, std::string& outError) override;
    bool Serialize(Json& out) const override;

    Transform2D&       Transform()       { return m_transform; }
    const Transform2D& Transform() const { return m_transform; }

private:
    Transform2D m_transform;
};

// ---------------------------------------------------------------------------
//  SpriteComponent - what the entity looks like.
// ---------------------------------------------------------------------------
class SpriteComponent;

// One row of the render system's list.
//
// It holds a copy of just the four things drawing needs, rather than a pointer
// back to the component, so the render loop reads one compact list instead of
// jumping around memory chasing pointers to whole objects.
struct SpriteRecord {
    Transform2D*     transform = nullptr;
    TextureRef       texture;
    Color            tint{};
    int              layer = 0;
    SpriteComponent* owner = nullptr;   // used when removing an entry
};

class SpriteRenderSystem {
public:
    static void Register(SpriteComponent& sprite);
    static void Unregister(SpriteComponent& sprite);

    // Draws every registered sprite, lowest layer first.
    static void Render(Camera& camera);

    static std::size_t Count();
    static void        Clear();
};

class SpriteComponent final : public Component {
public:
    static constexpr const char* kTypeName = "SpriteComponent";

    ~SpriteComponent() override;

    const char* TypeName() const override { return kTypeName; }

    // Scene file fields:
    //   "texture": "textures/player.bmp"   (required)
    //   "tint":    [r, g, b, a]            0-255 each; defaults to white
    //   "layer":   0                       higher numbers draw on top
    //   "size":    [w, h]                  in pixels; omit to use the image's own
    bool Deserialize(const Json& node, std::string& outError) override;
    bool Serialize(Json& out) const override;

    void OnAttach() override;
    void OnDetach() override;

    const TextureRef&  GetTexture() const { return m_texture; }
    const std::string& TexturePath() const { return m_texturePath; }
    Color              Tint() const  { return m_tint; }
    int                Layer() const { return m_layer; }
    Vec2               PixelSize() const { return m_pixelSize; }

    void SetTint(Color tint);
    void SetLayer(int layer);
    void SetTexture(std::string_view virtualPath);

private:
    friend class SpriteRenderSystem;

    TextureRef  m_texture;         // shared; see render/Texture.h
    std::string m_texturePath;
    Color       m_tint  = Color::White();
    int         m_layer = 0;
    Vec2        m_pixelSize{0.0f, 0.0f};   // (0,0) means "use the image's size"

    // Where this sprite's entry sits in the render system's list. -1 means it
    // is not registered.
    int m_recordIndex = -1;
};

} // namespace eng
