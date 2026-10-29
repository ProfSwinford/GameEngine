// ============================================================================
//  UI.cpp - laying out, operating and drawing the interface. See UI.h.
// ============================================================================

#include <engine/core/Json.h>
#include <engine/core/Log.h>
#include <engine/render/Camera.h>
#include <engine/render/Renderer.h>
#include <engine/resource/ResourceManager.h>
#include <engine/scene/Entity.h>
#include <engine/scene/Scene.h>
#include <engine/scene/UI.h>

#include <algorithm>
#include <vector>

namespace eng {

// ---------------------------------------------------------------------------
//  Names, for the scene file and the Inspector
// ---------------------------------------------------------------------------

const char* ToString(UISpace space) {
    return space == UISpace::World ? "World" : "Screen";
}

const char* ToString(UIAnchor anchor) {
    switch (anchor) {
        case UIAnchor::TopLeft:     return "TopLeft";
        case UIAnchor::Top:         return "Top";
        case UIAnchor::TopRight:    return "TopRight";
        case UIAnchor::Left:        return "Left";
        case UIAnchor::Center:      return "Center";
        case UIAnchor::Right:       return "Right";
        case UIAnchor::BottomLeft:  return "BottomLeft";
        case UIAnchor::Bottom:      return "Bottom";
        case UIAnchor::BottomRight: return "BottomRight";
    }
    return "TopLeft";
}

const char* ToString(UIAlign align) {
    switch (align) {
        case UIAlign::Left:   return "Left";
        case UIAlign::Center: return "Center";
        case UIAlign::Right:  return "Right";
    }
    return "Left";
}

bool UISpaceFromString(std::string_view text, UISpace& out) {
    if (text == "Screen") { out = UISpace::Screen; return true; }
    if (text == "World")  { out = UISpace::World;  return true; }
    return false;
}

bool UIAnchorFromString(std::string_view text, UIAnchor& out) {
    for (int i = 0; i <= static_cast<int>(UIAnchor::BottomRight); ++i) {
        const auto candidate = static_cast<UIAnchor>(i);
        if (text == ToString(candidate)) {
            out = candidate;
            return true;
        }
    }
    return false;
}

bool UIAlignFromString(std::string_view text, UIAlign& out) {
    for (int i = 0; i <= static_cast<int>(UIAlign::Right); ++i) {
        const auto candidate = static_cast<UIAlign>(i);
        if (text == ToString(candidate)) {
            out = candidate;
            return true;
        }
    }
    return false;
}

namespace {

// A colour, written as [255, 128, 0, 255].
//
// LOCAL TO THIS FILE rather than alongside ReadVec2 in core/Json.h. Color is
// declared in render/Renderer.h, and core must not depend on render just so
// that one reader can live with the others - the layering is worth more than
// the tidiness. Component.cpp reads its tint the same way, for the same
// reason.
Color ReadColor(const Json& node, std::string_view key, Color fallback) {
    if (!HasKey(node, key)) {
        return fallback;
    }
    const Json& value = node[std::string(key)];
    if (!value.is_array() || value.size() != 4) {
        return fallback;
    }

    // CLAMPED, not wrapped. A hand-typed 300 means "as much as there is",
    // and letting it overflow to 44 would turn a bright colour dark for a
    // reason nobody could see in the file.
    const auto channel = [](const Json& entry) -> unsigned char {
        if (!entry.is_number()) {
            return 255;
        }
        const int raw = entry.get<int>();
        return static_cast<unsigned char>(raw < 0 ? 0 : (raw > 255 ? 255 : raw));
    };
    return Color{channel(value[0]), channel(value[1]), channel(value[2]),
                 channel(value[3])};
}

// Where an anchor sits in a view of this size, in pixels.
Vec2 AnchorPoint(UIAnchor anchor, Vec2 viewport) {
    const float left   = 0.0f;
    const float middle = viewport.x * 0.5f;
    const float right  = viewport.x;
    const float top    = 0.0f;
    const float centre = viewport.y * 0.5f;
    const float bottom = viewport.y;

    switch (anchor) {
        case UIAnchor::TopLeft:     return Vec2{left,   top};
        case UIAnchor::Top:         return Vec2{middle, top};
        case UIAnchor::TopRight:    return Vec2{right,  top};
        case UIAnchor::Left:        return Vec2{left,   centre};
        case UIAnchor::Center:      return Vec2{middle, centre};
        case UIAnchor::Right:       return Vec2{right,  centre};
        case UIAnchor::BottomLeft:  return Vec2{left,   bottom};
        case UIAnchor::Bottom:      return Vec2{middle, bottom};
        case UIAnchor::BottomRight: return Vec2{right,  bottom};
    }
    return Vec2{left, top};
}

// An offset measured from a RIGHT or BOTTOM anchor runs the other way: "16
// in from the right edge" is x - 16, not x + 16. Without this, anchoring to
// the right would put the object off the side of the screen.
Vec2 DirectionFor(UIAnchor anchor) {
    float sx = 1.0f;
    float sy = 1.0f;
    if (anchor == UIAnchor::TopRight || anchor == UIAnchor::Right ||
        anchor == UIAnchor::BottomRight) {
        sx = -1.0f;
    }
    if (anchor == UIAnchor::BottomLeft || anchor == UIAnchor::Bottom ||
        anchor == UIAnchor::BottomRight) {
        sy = -1.0f;
    }
    return Vec2{sx, sy};
}

// ---- the pointer, set by whoever owns the view ----------------------------
Vec2 g_pointer{};
Vec2 g_pointerViewport{1280.0f, 720.0f};
bool g_pointerDown    = false;
bool g_pointerActive  = false;
bool g_pointerWasDown = false;

// Set the first time a host calls SetPointer, and never cleared. See the
// note on UpdateWindowPointer in UI.h.
bool g_hostDrivesPointer = false;

} // namespace

// ---------------------------------------------------------------------------
//  UIRectComponent
// ---------------------------------------------------------------------------

bool UIRectComponent::Deserialize(const Json& node, std::string& outError) {
    UISpaceFromString(ReadString(node, "space", ToString(space)), space);
    UIAnchorFromString(ReadString(node, "anchor", ToString(anchor)), anchor);
    offset    = ReadVec2(node, "offset", offset);
    size      = ReadVec2(node, "size", size);
    pivot     = ReadVec2(node, "pivot", pivot);
    sortOrder = ReadInt(node, "sortOrder", sortOrder);
    visible   = ReadBool(node, "visible", visible);
    outError.clear();
    return true;
}

bool UIRectComponent::Serialize(Json& out) const {
    out["space"]     = ToString(space);
    out["anchor"]    = ToString(anchor);
    out["offset"]    = {offset.x, offset.y};
    out["size"]      = {size.x, size.y};
    out["pivot"]     = {pivot.x, pivot.y};
    out["sortOrder"] = sortOrder;
    out["visible"]   = visible;
    return true;
}

// ---------------------------------------------------------------------------
//  UITextComponent
// ---------------------------------------------------------------------------

bool UITextComponent::Deserialize(const Json& node, std::string& outError) {
    text     = ReadString(node, "text", text);
    fontPath = ReadString(node, "font", fontPath);
    fontSize = ReadFloat(node, "fontSize", fontSize);
    color    = ReadColor(node, "color", color);
    UIAlignFromString(ReadString(node, "align", ToString(align)), align);
    middle = ReadBool(node, "middle", middle);
    outError.clear();
    return true;
}

bool UITextComponent::Serialize(Json& out) const {
    out["text"]     = text;
    out["font"]     = fontPath;
    out["fontSize"] = fontSize;
    out["color"]    = {color.r, color.g, color.b, color.a};
    out["align"]    = ToString(align);
    out["middle"]   = middle;
    return true;
}

const FontRef& UITextComponent::ResolvedFont() {
    // RE-FETCHED ONLY WHEN SOMETHING CHANGED. Asking the cache every frame
    // would be a string compare and a hash per label per frame, for an
    // answer that changes when somebody edits a field in the Inspector.
    if (!m_font || m_fontPathUsed != fontPath || m_fontSizeUsed != fontSize) {
        m_font         = ResourceManager::LoadFont(fontPath, fontSize);
        m_fontPathUsed = fontPath;
        m_fontSizeUsed = fontSize;
    }
    return m_font;
}

// ---------------------------------------------------------------------------
//  UIImageComponent
// ---------------------------------------------------------------------------

bool UIImageComponent::Deserialize(const Json& node, std::string& outError) {
    texturePath      = ReadString(node, "texture", texturePath);
    tint             = ReadColor(node, "tint", tint);
    outlineThickness = ReadFloat(node, "outline", outlineThickness);
    outError.clear();
    return true;
}

bool UIImageComponent::Serialize(Json& out) const {
    out["texture"] = texturePath;
    out["tint"]    = {tint.r, tint.g, tint.b, tint.a};
    out["outline"] = outlineThickness;
    return true;
}

const TextureRef& UIImageComponent::ResolvedTexture() {
    if (m_texturePathUsed != texturePath) {
        m_texture         = texturePath.empty() ? TextureRef{}
                                                : ResourceManager::LoadTexture(texturePath);
        m_texturePathUsed = texturePath;
    }
    return m_texture;
}

// ---------------------------------------------------------------------------
//  UIButtonComponent
// ---------------------------------------------------------------------------

bool UIButtonComponent::Deserialize(const Json& node, std::string& outError) {
    interactable = ReadBool(node, "interactable", interactable);
    normalTint   = ReadColor(node, "normal", normalTint);
    hoverTint    = ReadColor(node, "hover", hoverTint);
    pressedTint  = ReadColor(node, "pressed", pressedTint);
    disabledTint = ReadColor(node, "disabled", disabledTint);
    outError.clear();
    return true;
}

bool UIButtonComponent::Serialize(Json& out) const {
    out["interactable"] = interactable;
    out["normal"]   = {normalTint.r,   normalTint.g,   normalTint.b,   normalTint.a};
    out["hover"]    = {hoverTint.r,    hoverTint.g,    hoverTint.b,    hoverTint.a};
    out["pressed"]  = {pressedTint.r,  pressedTint.g,  pressedTint.b,  pressedTint.a};
    out["disabled"] = {disabledTint.r, disabledTint.g, disabledTint.b, disabledTint.a};
    return true;
}

// ---------------------------------------------------------------------------
//  UISliderComponent
// ---------------------------------------------------------------------------

bool UISliderComponent::Deserialize(const Json& node, std::string& outError) {
    minValue     = ReadFloat(node, "min", minValue);
    maxValue     = ReadFloat(node, "max", maxValue);
    value        = ReadFloat(node, "value", value);
    interactable = ReadBool(node, "interactable", interactable);
    trackColor   = ReadColor(node, "track", trackColor);
    fillColor    = ReadColor(node, "fill", fillColor);
    knobColor    = ReadColor(node, "knob", knobColor);
    outError.clear();
    return true;
}

bool UISliderComponent::Serialize(Json& out) const {
    out["min"]          = minValue;
    out["max"]          = maxValue;
    out["value"]        = value;
    out["interactable"] = interactable;
    out["track"] = {trackColor.r, trackColor.g, trackColor.b, trackColor.a};
    out["fill"]  = {fillColor.r,  fillColor.g,  fillColor.b,  fillColor.a};
    out["knob"]  = {knobColor.r,  knobColor.g,  knobColor.b,  knobColor.a};
    return true;
}

float UISliderComponent::Normalised() const {
    const float span = maxValue - minValue;
    if (span <= 0.0001f) {
        return 0.0f;   // a slider with no range is not a bug worth crashing on
    }
    const float t = (value - minValue) / span;
    return (t < 0.0f) ? 0.0f : (t > 1.0f ? 1.0f : t);
}

// ---------------------------------------------------------------------------
//  UISystem
// ---------------------------------------------------------------------------

void UISystem::RegisterComponentTypes() {
    ComponentFactory::Register(UIRectComponent::kTypeName,
                               []() -> std::unique_ptr<Component> {
                                   return std::make_unique<UIRectComponent>();
                               });
    ComponentFactory::Register(UITextComponent::kTypeName,
                               []() -> std::unique_ptr<Component> {
                                   return std::make_unique<UITextComponent>();
                               });
    ComponentFactory::Register(UIImageComponent::kTypeName,
                               []() -> std::unique_ptr<Component> {
                                   return std::make_unique<UIImageComponent>();
                               });
    ComponentFactory::Register(UIButtonComponent::kTypeName,
                               []() -> std::unique_ptr<Component> {
                                   return std::make_unique<UIButtonComponent>();
                               });
    ComponentFactory::Register(UISliderComponent::kTypeName,
                               []() -> std::unique_ptr<Component> {
                                   return std::make_unique<UISliderComponent>();
                               });
}

void UISystem::SetPointer(Vec2 positionInView, Vec2 viewportSize, bool down,
                          bool active) {
    g_pointer           = positionInView;
    g_pointerViewport   = viewportSize;
    g_pointerDown       = down;
    g_pointerActive     = active;
    g_hostDrivesPointer = true;
}

void UISystem::UpdateWindowPointer(Vec2 positionInWindow, Vec2 windowSize, bool down) {
    if (g_hostDrivesPointer) {
        return;   // the editor is in charge; see UI.h
    }
    g_pointer         = positionInWindow;
    g_pointerViewport = windowSize;
    g_pointerDown     = down;
    g_pointerActive   = true;
}

namespace {

// Works out where one rect lands this frame and stores it on the component.
void ResolveRect(Entity& entity, UIRectComponent& rect, const Camera& camera,
                 Vec2 viewport) {
    Vec2 size = rect.size;
    Vec2 anchorPos;

    if (rect.space == UISpace::World) {
        // THE TRANSFORM DECIDES, projected like a sprite. Scaled by the
        // camera's zoom as well, or a label over an enemy's head would stay
        // the same size while the enemy grew.
        anchorPos = camera.WorldToScreen(entity.Transform().WorldPosition());
        size      = size * camera.Zoom();
    } else {
        const Vec2 base      = AnchorPoint(rect.anchor, viewport);
        const Vec2 direction = DirectionFor(rect.anchor);
        anchorPos = Vec2{base.x + rect.offset.x * direction.x,
                         base.y + rect.offset.y * direction.y};
    }

    // The pivot says which point of the object sits at anchorPos.
    const Vec2 min{anchorPos.x - size.x * rect.pivot.x,
                   anchorPos.y - size.y * rect.pivot.y};

    rect.resolvedMin = min;
    rect.resolvedMax = Vec2{min.x + size.x, min.y + size.y};
}

// Every entity with a UIRect, in the order they should be drawn.
struct Item {
    Entity*          entity = nullptr;
    UIRectComponent* rect   = nullptr;
};

std::vector<Item> CollectSorted(Scene& scene, const Camera& camera, Vec2 viewport) {
    std::vector<Item> items;
    scene.ForEach([&](Entity& entity) {
        auto* rect = entity.Find<UIRectComponent>();
        if (rect == nullptr || !rect->visible) {
            return;
        }
        ResolveRect(entity, *rect, camera, viewport);
        items.push_back(Item{&entity, rect});
    });

    // STABLE, so two objects with the same sortOrder keep the order the scene
    // file gave them rather than swapping about between runs.
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return a.rect->sortOrder < b.rect->sortOrder;
    });
    return items;
}

} // namespace

void UISystem::BeginStep(Scene& scene) {
    scene.ForEach([](Entity& entity) {
        if (auto* button = entity.Find<UIButtonComponent>()) {
            button->m_clicked = false;
        }
        if (auto* slider = entity.Find<UISliderComponent>()) {
            slider->m_changed = false;
        }
    });
}

void UISystem::Update(Scene& scene, const Camera& camera) {
    BeginStep(scene);

    // THE VIEWPORT THAT CAME WITH THE POINTER, not whatever is currently
    // being rendered into - see the note on SetPointer in UI.h. Laying out
    // against one size and clicking against another is a miss nobody can
    // see the cause of.
    std::vector<Item> items = CollectSorted(scene, camera, g_pointerViewport);

    // ---- who is under the pointer -----------------------------------------
    //
    // SEARCHED BACK TO FRONT, so the thing drawn on top is the thing you
    // click. Front-to-back would let a panel behind a button steal the press
    // that visibly landed on the button.
    Entity* hot = nullptr;
    if (g_pointerActive) {
        for (auto it = items.rbegin(); it != items.rend(); ++it) {
            const bool interactive = it->entity->Find<UIButtonComponent>() != nullptr ||
                                     it->entity->Find<UISliderComponent>() != nullptr;
            if (interactive && it->rect->Contains(g_pointer)) {
                hot = it->entity;
                break;
            }
        }
    }

    const bool pressedThisStep  = g_pointerDown && !g_pointerWasDown;
    const bool releasedThisStep = !g_pointerDown && g_pointerWasDown;

    for (Item& item : items) {
        const bool over = (item.entity == hot);

        if (auto* button = item.entity->Find<UIButtonComponent>()) {
            button->m_hovered = over && button->interactable;

            if (!button->interactable) {
                button->m_pressed = false;
            } else {
                if (pressedThisStep && over) {
                    button->m_pressed = true;
                }
                if (releasedThisStep) {
                    // A CLICK IS PRESS AND RELEASE ON THE SAME BUTTON.
                    // Pressing and sliding off is how somebody changes their
                    // mind, and it must not count.
                    if (button->m_pressed && over) {
                        button->m_clicked = true;
                    }
                    button->m_pressed = false;
                }
            }
        }

        if (auto* slider = item.entity->Find<UISliderComponent>()) {
            if (!slider->interactable) {
                slider->m_dragging = false;
                continue;
            }
            if (pressedThisStep && over) {
                slider->m_dragging = true;
            }
            if (!g_pointerDown) {
                slider->m_dragging = false;
            }

            if (slider->m_dragging) {
                const float width = item.rect->resolvedMax.x - item.rect->resolvedMin.x;
                if (width > 0.0001f) {
                    float t = (g_pointer.x - item.rect->resolvedMin.x) / width;
                    t       = (t < 0.0f) ? 0.0f : (t > 1.0f ? 1.0f : t);
                    const float next =
                        slider->minValue + t * (slider->maxValue - slider->minValue);
                    if (next != slider->value) {
                        slider->value     = next;
                        slider->m_changed = true;
                    }
                }
            }
        }
    }

    g_pointerWasDown = g_pointerDown;
}

void UISystem::Render(Scene& scene, const Camera& camera, Vec2 viewportSize) {
    std::vector<Item> items = CollectSorted(scene, camera, viewportSize);

    for (Item& item : items) {
        const Vec2 min = item.rect->resolvedMin;
        const Vec2 max = item.rect->resolvedMax;

        // ---- the background ------------------------------------------------
        if (auto* image = item.entity->Find<UIImageComponent>()) {
            Color tint = image->tint;

            // A button drives its own image's colour, which is what makes
            // hover and press visible without anybody wiring anything up.
            if (auto* button = item.entity->Find<UIButtonComponent>()) {
                tint = !button->interactable ? button->disabledTint
                       : button->m_pressed   ? button->pressedTint
                       : button->m_hovered   ? button->hoverTint
                                             : button->normalTint;
            }

            if (const TextureRef& texture = image->ResolvedTexture()) {
                const Vec2 centre{(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f};
                Renderer::DrawSprite(texture, centre, Vec2{max.x - min.x, max.y - min.y},
                                     0.0f, tint);
            } else if (image->outlineThickness > 0.0f) {
                Renderer::DrawRect(min, max, tint);
            } else {
                Renderer::DrawFilledRect(min, max, tint);
            }
        }

        // ---- the slider ----------------------------------------------------
        if (auto* slider = item.entity->Find<UISliderComponent>()) {
            Renderer::DrawFilledRect(min, max, slider->trackColor);

            const float t = slider->Normalised();
            const float fillRight = min.x + (max.x - min.x) * t;
            if (fillRight > min.x) {
                Renderer::DrawFilledRect(min, Vec2{fillRight, max.y}, slider->fillColor);
            }

            // The knob, kept inside the track at both ends rather than
            // hanging half off it.
            const float half   = (max.y - min.y) * 0.5f;
            const float knobX  = std::clamp(fillRight, min.x + half, max.x - half);
            Renderer::DrawFilledRect(Vec2{knobX - half * 0.6f, min.y},
                                     Vec2{knobX + half * 0.6f, max.y},
                                     slider->knobColor);
        }

        // ---- the label -----------------------------------------------------
        if (auto* label = item.entity->Find<UITextComponent>()) {
            const FontRef& font = label->ResolvedFont();
            if (!font) {
                continue;
            }

            // The whole block, used only to place it vertically.
            const Vec2  block = font->Measure(label->text);
            const float top   = label->middle
                                    ? (min.y + max.y) * 0.5f - block.y * 0.5f
                                    : min.y;

            // ---- EACH LINE IS ALIGNED ON ITS OWN ---------------------------
            //
            // Not the block. Centring the block and then drawing every line
            // from its left edge leaves the short lines visibly
            // left-aligned inside a centred box - which looks like centring
            // is broken, because for every line but the longest it is.
            //
            // Done line by line here rather than by handing the whole string
            // to the renderer, because only this code knows how wide the box
            // is and which way the text is meant to sit in it.
            float lineY = top;
            ForEachLine(label->text, [&](std::string_view line) {
                const float lineWidth = font->Measure(line).x;

                float x = min.x;
                if (label->align == UIAlign::Center) {
                    x = (min.x + max.x) * 0.5f - lineWidth * 0.5f;
                } else if (label->align == UIAlign::Right) {
                    x = max.x - lineWidth;
                }

                Renderer::DrawTextFont(font, Vec2{x, lineY}, line, label->color);
                lineY += font->LineHeight();
            });
        }
    }
}

} // namespace eng
