#pragma once

// ============================================================================
//  UI.h - on-screen interface built out of ENTITIES AND COMPONENTS, like
//  everything else in this engine.
//
//  A button is not a special kind of object the engine knows about. It is an
//  entity with a UIRect saying where it is and a UIButton saying how it
//  behaves - so it can be selected in the Hierarchy, edited in the Inspector,
//  parented to something, saved into a scene file and reached from a script,
//  all using machinery that already existed.
//
//  ==========================================================================
//  WHY THIS IS NOT DRAWN WITH GIZMOS
//
//  It used to be. A game's score was a Gizmos::Text in screen space, and that
//  worked until the Game view started asking for the world WITHOUT gizmos -
//  which it must, because gizmos are the grid, the origin arrows and the
//  collider outlines, and none of those ship with a game. The score went with
//  them, and the interface vanished from the only view that is supposed to
//  show what a player sees.
//
//  Gizmos are a DEBUG overlay. A UI is part of the game. Mixing the two means
//  every switch that turns one off takes the other with it, so they are now
//  separate systems drawn at separate stages.
//
//  ==========================================================================
//  TWO SPACES, chosen per object.
//
//    Screen - positioned in PIXELS from a corner or edge of the view. A
//             score, a health bar, a menu. The camera does not affect it, so
//             it stays put while the world scrolls underneath.
//
//    World  - positioned at the entity's own TRANSFORM, projected through the
//             camera like a sprite. A name over an enemy's head, a damage
//             number, a speech bubble. It moves and scales with the world.
//
//  Screen space reads its position from UIRect::offset and ignores the
//  transform; world space reads the transform and ignores the offset. One
//  field is in charge in each case, rather than both contributing a bit.
//
//  ==========================================================================
//  WHAT TO PUT ON AN ENTITY
//
//    Text   : UIRect + UIText
//    Image  : UIRect + UIImage
//    Button : UIRect + UIImage (the background) + UIText (the label)
//             + UIButton (the behaviour)
//    Slider : UIRect + UISlider
//
//  A button being three components rather than one is deliberate: its
//  background is an image like any other image, and its label is text like
//  any other text. Nothing about them is button-specific, so nothing about
//  them is duplicated.
// ============================================================================

#include <engine/math/Vec2.h>
#include <engine/render/Font.h>
#include <engine/render/Renderer.h>   // Color, and the drawing the system does
#include <engine/render/Texture.h>
#include <engine/scene/Component.h>
#include <engine/scene/SystemOrder.h>

#include <string>

namespace eng {

class Camera;
class Scene;

enum class UISpace {
    Screen,
    World,
};

// Which point of the VIEW an offset is measured from. Only meaningful in
// screen space.
//
// This is what makes a layout survive the window being resized: "16 pixels in
// from the top-left" and "16 pixels up from the bottom-right" both stay
// correct at any size, where a single absolute position cannot.
enum class UIAnchor {
    TopLeft,    Top,     TopRight,
    Left,       Center,  Right,
    BottomLeft, Bottom,  BottomRight,
};

enum class UIAlign {
    Left,
    Center,
    Right,
};

const char* ToString(UISpace space);
const char* ToString(UIAnchor anchor);
const char* ToString(UIAlign align);

bool UISpaceFromString(std::string_view text, UISpace& out);
bool UIAnchorFromString(std::string_view text, UIAnchor& out);
bool UIAlignFromString(std::string_view text, UIAlign& out);

// ---------------------------------------------------------------------------
//  UIRectComponent - where a UI object is and how big it is.
//
//  Every UI object has one. It is to the interface what Transform2D is to the
//  world, and like the transform it carries no appearance of its own: an
//  entity with only a UIRect draws nothing.
// ---------------------------------------------------------------------------
class UIRectComponent final : public Component {
public:
    static constexpr const char* kTypeName = "UIRectComponent";
    const char* TypeName() const override { return kTypeName; }

    bool Deserialize(const Json& node, std::string& outError) override;
    bool Serialize(Json& out) const override;

    UISpace  space  = UISpace::Screen;
    UIAnchor anchor = UIAnchor::TopLeft;

    // In screen space: pixels from the anchor. In world space: ignored, and
    // the entity's transform decides.
    Vec2 offset{16.0f, 16.0f};

    Vec2 size{200.0f, 40.0f};

    // Which point of the object sits at the position above, as a fraction of
    // its own size. {0,0} is its top-left corner and {0.5,0.5} its middle.
    Vec2 pivot{0.0f, 0.0f};

    // Higher draws later, so higher is in front. Sorting by this rather than
    // by the order entities happen to sit in the scene is what makes layering
    // something you control instead of something you discover.
    int sortOrder = 0;

    bool visible = true;

    // Where this ends up on screen this frame, in view pixels. Worked out by
    // UISystem and stored so that input and drawing agree without computing
    // it twice - and so the Inspector can show it.
    Vec2 resolvedMin{};
    Vec2 resolvedMax{};

    bool Contains(Vec2 point) const {
        return point.x >= resolvedMin.x && point.x <= resolvedMax.x &&
               point.y >= resolvedMin.y && point.y <= resolvedMax.y;
    }
};

// ---------------------------------------------------------------------------
//  UITextComponent - a label.
// ---------------------------------------------------------------------------
class UITextComponent final : public Component {
public:
    static constexpr const char* kTypeName = "UITextComponent";
    const char* TypeName() const override { return kTypeName; }

    bool Deserialize(const Json& node, std::string& outError) override;
    bool Serialize(Json& out) const override;

    void OnDetach() override { m_font.reset(); }

    std::string text = "Text";

    // A .ttf under assets/, e.g. "fonts/Inter-Regular.ttf". EMPTY MEANS THE
    // BUILT-IN FONT, which is why a freshly created label is readable before
    // anybody has gone looking for a font file.
    std::string fontPath;

    float   fontSize = 18.0f;
    Color   color    = Color::White();
    UIAlign align    = UIAlign::Left;

    // Vertically centred inside the rect. Off means it starts at the top.
    bool middle = true;

    // The baked font, fetched on demand and re-fetched when the path or size
    // changes. Held as a shared reference so editing the size in the
    // Inspector lets go of the old atlas.
    const FontRef& ResolvedFont();

private:
    FontRef     m_font;
    std::string m_fontPathUsed;
    float       m_fontSizeUsed = 0.0f;
};

// ---------------------------------------------------------------------------
//  UIImageComponent - a rectangle, with or without a texture.
//
//  With no texture it is a plain filled rectangle, which is what makes it
//  useful as a panel behind other things and as a button's background.
// ---------------------------------------------------------------------------
class UIImageComponent final : public Component {
public:
    static constexpr const char* kTypeName = "UIImageComponent";
    const char* TypeName() const override { return kTypeName; }

    bool Deserialize(const Json& node, std::string& outError) override;
    bool Serialize(Json& out) const override;

    void OnDetach() override { m_texture.reset(); }

    std::string texturePath;
    Color       tint = Color{60, 64, 78, 230};

    // Drawn as an outline instead of a fill. A thickness of 0 means filled.
    float outlineThickness = 0.0f;

    const TextureRef& ResolvedTexture();

private:
    TextureRef  m_texture;
    std::string m_texturePathUsed;
};

// ---------------------------------------------------------------------------
//  UIButtonComponent - something to press.
//
//  It tints whichever UIImage is on the same entity, and reports a click.
// ---------------------------------------------------------------------------
class UIButtonComponent final : public Component {
public:
    static constexpr const char* kTypeName = "UIButtonComponent";
    const char* TypeName() const override { return kTypeName; }

    bool Deserialize(const Json& node, std::string& outError) override;
    bool Serialize(Json& out) const override;

    bool  interactable = true;
    Color normalTint   = Color{60, 64, 78, 230};
    Color hoverTint    = Color{80, 88, 106, 240};
    Color pressedTint  = Color{40, 44, 54, 255};
    Color disabledTint = Color{45, 46, 52, 150};

    // ---- what a script reads ----------------------------------------------
    //
    //     if (auto* b = Owner()->Find<eng::UIButtonComponent>(); b && b->WasClicked())
    //
    // POLLED rather than a callback. A callback would mean a script handing
    // the engine a function pointer that outlives the script when the scene
    // reloads, which is the sort of lifetime problem this engine avoids on
    // purpose. A flag that is true for exactly one step has no such problem.
    //
    // A click is press AND release on the same button, which is what every
    // interface does: pressing and sliding off is how you change your mind.
    bool WasClicked() const { return m_clicked; }
    bool IsHovered() const  { return m_hovered; }
    bool IsPressed() const  { return m_pressed; }

    // Set by UISystem. Not private only because the system is not a friend
    // and making it one to set three booleans would be ceremony.
    bool m_clicked = false;
    bool m_hovered = false;
    bool m_pressed = false;
};

// ---------------------------------------------------------------------------
//  UISliderComponent - a value you can drag.
// ---------------------------------------------------------------------------
class UISliderComponent final : public Component {
public:
    static constexpr const char* kTypeName = "UISliderComponent";
    const char* TypeName() const override { return kTypeName; }

    bool Deserialize(const Json& node, std::string& outError) override;
    bool Serialize(Json& out) const override;

    float minValue = 0.0f;
    float maxValue = 1.0f;
    float value    = 0.5f;

    bool  interactable = true;
    Color trackColor   = Color{40, 42, 50, 230};
    Color fillColor    = Color{90, 150, 230, 255};
    Color knobColor    = Color{220, 224, 232, 255};

    // True on the step the value was changed by dragging, so a script can
    // react to a change rather than re-reading the value every frame.
    bool WasChanged() const { return m_changed; }

    // 0..1, which is what the drawing needs and what a script usually wants.
    float Normalised() const;

    bool m_changed = false;
    bool m_dragging = false;
};

// ---------------------------------------------------------------------------
//  UISystem - lays the interface out, handles the pointer, and draws it.
// ---------------------------------------------------------------------------
class UISystem {
public:
    static void RegisterComponentTypes();

    // WHERE THE POINTER IS, in the pixels of the view the UI is drawn into,
    // AND HOW BIG THAT VIEW IS.
    //
    // The size travels with the position because the two have to agree. A
    // rect anchored to the bottom-right is laid out from the view's size, so
    // testing a click against a layout computed for a DIFFERENT size would
    // miss by however much the two differ - and in the editor they differ by
    // the whole window border. The engine cannot look the size up itself at
    // the moment it needs it, because the Game view's picture is not the
    // current render target while the simulation is running.
    //
    // Called by whoever owns the view. In the editor that is the Game panel,
    // which subtracts its own corner; the engine never has to know a panel
    // exists. Calling this once marks the pointer as host-driven and stops
    // the window fallback below from overwriting it.
    //
    // `active` false means "no pointer here at all" - the mouse is over some
    // other panel, or the game is not running - and nothing hovers.
    static void SetPointer(Vec2 positionInView, Vec2 viewportSize, bool down,
                           bool active);

    // The fallback for a SHIPPED GAME, where the window is the view and the
    // mouse position already means what it says. Does nothing once a host
    // has taken over with SetPointer, so the editor's value is not trampled
    // by the engine's every frame.
    static void UpdateWindowPointer(Vec2 positionInWindow, Vec2 windowSize, bool down);

    // Works out every rect's position, then updates buttons and sliders.
    // Called once per step so that WasClicked lines up with a script's
    // OnFixedUpdate. Uses the viewport that came with the pointer.
    static void Update(Scene& scene, const Camera& camera);

    // Draws. Separate from Update because the editor draws the same scene
    // into two views and must not process a click twice.
    static void Render(Scene& scene, const Camera& camera, Vec2 viewportSize);

    // Clears the per-step flags. Called at the top of a step.
    static void BeginStep(Scene& scene);
};

} // namespace eng
