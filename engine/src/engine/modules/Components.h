#pragma once

// ============================================================================
//  Components.h - what a component is, the order systems run in, and the built-in ones.
//
//  Each section below was its own file until the engine was reorganised; the
//  banner at the top of each one still explains that piece on its own.
// ============================================================================

#include <engine/modules/Entities.h>
#include <engine/modules/Math.h>
#include <engine/modules/Rendering.h>
#include <engine/modules/Settings.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

// ============================================================================
//  SystemOrder.h - systems run in a written-down order, not whichever order
//  they happened to be created in.
//
//  WHY THAT MATTERS, CONCRETELY
//  Run collision BEFORE movement and it checks where everything was LAST
//  frame, so a fast object passes straight through a wall and no collision is
//  ever reported. Run input AFTER movement and the controls lag by a frame -
//  a small, horrible, hard-to-describe problem that players feel immediately.
//
//  An accidental order works right up until somebody adds a system, and then
//  it breaks something apparently unrelated.
//
//  ==========================================================================
//  THE ORDER. Written to the log once at start-up, so it is never a guess.
//
//    100 Input              read what the player wants this tick
//    200 Gameplay / AI      decide what everything is going to do
//    300 Movement           actually move things
//    400 Collision          check overlaps at the NEW positions
//    500 CollisionResponse  deliver the queued messages
//    600 Deferred           create and destroy entities
//    700 Camera             follow whatever it follows, after it has moved
//    800 Render             draw the settled result
//    900 Gizmos             draw helper shapes on top
//
//  Stages 100-700 run once per FIXED SIMULATION STEP. Stages 800 and above run
//  once per drawn frame however many steps that frame took, because drawing
//  the same scene three times would cost three times as much for one picture.
//
//  THE PAIR THAT MATTERS MOST: Movement (300) before Collision (400). Swap
//  them and a fast object is tested where it WAS, moves through the wall, and
//  is tested again on the far side - so it passes through solid objects with
//  no collision ever firing.
//
//  Note stage 600. Entities are created and destroyed at ONE known point,
//  never in the middle of a system walking its own list. See DeferredOps.h.
//  ==========================================================================
// ============================================================================


namespace eng {

// The stage numbers. A plain integer priority is entirely adequate here; the
// alternative (declaring which system depends on which and sorting that out
// automatically) is a lot of machinery for a list this short.
namespace SystemStage {
inline constexpr int kInput             = 100;
inline constexpr int kGameplay          = 200;
inline constexpr int kMovement          = 300;
inline constexpr int kCollision         = 400;
inline constexpr int kCollisionResponse = 500;
inline constexpr int kDeferred          = 600;
inline constexpr int kCamera            = 700;
inline constexpr int kRender            = 800;
inline constexpr int kGizmos            = 900;

// Everything below this runs per simulation step; everything at or above it
// runs once per drawn frame.
inline constexpr int kFirstRenderStage = kRender;
} // namespace SystemStage

// Anything that needs to run every tick. Subclass it, say which stage it
// belongs to, and register it with the scheduler.
class System {
public:
    virtual ~System() = default;

    // `deltaSeconds` is the FIXED step for simulation systems and the real
    // frame time for render-stage ones.
    //
    // It is handed IN rather than looked up. A system that read a clock for
    // itself would make the simulation depend on the frame rate again, which
    // is the whole thing this design exists to avoid.
    virtual void Update(float deltaSeconds) = 0;

    virtual const char* Name() const  = 0;
    virtual int         Order() const = 0;
};

class SystemScheduler {
public:
    // The scheduler does NOT own the system - it only borrows the pointer, so
    // a system must Unregister before it is destroyed.
    static void Register(System* system);
    static void Unregister(System* system);
    static void Clear();

    // Runs every system whose stage number is in [minOrder, maxOrder).
    static void UpdateRange(int minOrder, int maxOrder, float deltaSeconds);

    // Simulation stages, once per fixed step.
    static void Simulate(float fixedStepSeconds);
    // Render stages, once per drawn frame.
    static void RenderPass(float realDeltaSeconds);

    // Writes the running order to the log at start-up. Worth having the first
    // time something happens a frame later than expected.
    static void LogOrder();

    static void        ForEach(const std::function<void(System&)>& fn);
    static std::size_t Count();
};

} // namespace eng


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
    TextureRef  m_texture;         // shared; see render/Texture.h
    std::string m_texturePath;
    Color       m_tint  = Color::White();
    int         m_layer = 0;
    Vec2        m_pixelSize{0.0f, 0.0f};   // (0,0) means "use the image's size"
};

} // namespace eng


// ============================================================================
//  SpinComponent.h - makes an entity turn on the spot, forever.
//
//  It adds `radiansPerSecond * deltaSeconds` to its own transform's rotation
//  every simulation step. That is the entire component.
//
//  WHY ONE FIELD IS ENOUGH TO PRODUCE A WHOLE ORBITING SOLAR SYSTEM
//  There is no orbit code here, and none is needed. Spinning a PARENT sweeps
//  everything attached to it around in a circle, because a child's position in
//  the world is worked out through its parent's transform. That is what the
//  parent/child transform tree in Transform2D.h buys.
//
//  So in assets/scenes/orbit_test.json:
//     the root spins   -> the planet (its child, offset sideways) ORBITS the centre
//     the planet spins -> the moon (its child) orbits the planet, and the
//                         planet visibly turns as well
//     the moon spins   -> the moon turns on its own axis
//
//  Three numbers in a data file produce a three-deep orbiting system, with no
//  code written for any of it.
//
//  It is also a good first component to copy when writing your own: it is
//  small enough to read in one go and it shows every piece - the type name,
//  loading and saving, attach and detach, and the system that updates it.
// ============================================================================


namespace eng {

class SpinComponent final : public Component {
public:
    static constexpr const char* kTypeName = "SpinComponent";

    ~SpinComponent() override;

    const char* TypeName() const override { return kTypeName; }

    // Scene file fields - give ONE of these:
    //   "radiansPerSecond": 0.6     negative turns the other way
    //   "degreesPerSecond": 34.4    the same thing in the unit people think in
    bool Deserialize(const Json& node, std::string& outError) override;
    bool Serialize(Json& out) const override;

    void OnAttach() override;
    void OnDetach() override;

    float RadiansPerSecond() const { return m_radiansPerSecond; }
    void  SetRadiansPerSecond(float rate) { m_radiansPerSecond = rate; }

private:
    float m_radiansPerSecond = 0.0f;
};

// Turns every attached SpinComponent, once per simulation step.
//
// It runs at stage 300 (Movement), which is BEFORE collision at stage 400 - so
// collisions are checked at the positions things actually moved to this tick
// rather than where they were last tick. That is the ordering pair
// SystemOrder.h calls out by name.
class SpinSystem final : public System {
public:
    void        Update(float deltaSeconds) override;
    const char* Name() const override  { return "SpinSystem"; }
    int         Order() const override { return SystemStage::kMovement; }

    static void        Register(SpinComponent& spin);
    static void        Unregister(SpinComponent& spin);
    static void        Clear();
    static std::size_t Count();

    // Tells the ComponentFactory that "SpinComponent" in a scene file means
    // this class.
    static void RegisterComponentTypes();
};

} // namespace eng
