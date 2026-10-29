// ============================================================================
//  ObjectMenu.cpp - the one list of things you can add. See ObjectMenu.h.
// ============================================================================

#include "ObjectMenu.h"

#include "EditorApp.h"

#include <engine/Engine.h>
#include <engine/core/Log.h>
#include <engine/scene/CameraComponent.h>
#include <engine/scene/Entity.h>
#include <engine/scene/Scene.h>
#include <engine/scene/UI.h>

#include <string_view>

#include <imgui.h>

namespace editor {
namespace {

// A kind of object carries a LIST of components, because the interesting
// ones are not a single component. A Button is a rect saying where it is, an
// image for its background, a label, and the behaviour that ties them
// together - four ordinary components rather than one special "button" type.
// That is the component model working as intended, and the menu exists so
// nobody has to assemble it by hand every time.
inline constexpr int kMaxComponents = 4;

struct ObjectKind {
    const char* name;

    // nullptr puts it at the top level; "UI" puts it in the UI submenu.
    const char* group;

    const char* components[kMaxComponents];
    const char* tip;
};

// THE LIST. Adding a kind of object is one row here and it appears in both
// menus at once.
constexpr ObjectKind kKinds[] = {
    {"Empty", nullptr, {nullptr, nullptr, nullptr, nullptr},
     "Just a transform. Add components to it yourself."},

    {"Camera", nullptr, {eng::CameraComponent::kTypeName, nullptr, nullptr, nullptr},
     "The view the game is drawn through. It is an entity like any other, so "
     "you can parent it, script it, and see where it is."},

    {"Sprite", nullptr, {eng::SpriteComponent::kTypeName, nullptr, nullptr, nullptr},
     "An entity with a SpriteComponent, ready for an image to be dropped on "
     "it."},

    // ---- the interface ---------------------------------------------------
    {"Text", "UI",
     {eng::UIRectComponent::kTypeName, eng::UITextComponent::kTypeName, nullptr,
      nullptr},
     "A label. Give it a .ttf in the Inspector, or leave the font blank for "
     "the built-in one."},

    {"Image", "UI",
     {eng::UIRectComponent::kTypeName, eng::UIImageComponent::kTypeName, nullptr,
      nullptr},
     "A rectangle. With no texture it is a plain colour, which is what makes "
     "it useful as a panel behind other things."},

    {"Button", "UI",
     {eng::UIRectComponent::kTypeName, eng::UIImageComponent::kTypeName,
      eng::UITextComponent::kTypeName, eng::UIButtonComponent::kTypeName},
     "A background, a label and the behaviour. Ask it WasClicked() from a "
     "script."},

    {"Slider", "UI",
     {eng::UIRectComponent::kTypeName, eng::UISliderComponent::kTypeName, nullptr,
      nullptr},
     "A value you can drag between a minimum and a maximum."},
};

// The defaults that make a freshly created object look like what it is,
// rather than like the generic rect every UI object starts as.
void ApplyUIDefaults(std::string_view kind, eng::Entity& entity) {
    auto* rect = entity.Find<eng::UIRectComponent>();
    auto* text = entity.Find<eng::UITextComponent>();

    if (rect != nullptr) {
        // CENTRED ON SCREEN, so a new object lands somewhere visible rather
        // than in the top-left corner under everything created before it.
        rect->anchor = eng::UIAnchor::Center;
        rect->pivot  = eng::Vec2{0.5f, 0.5f};
        rect->offset = eng::Vec2{0.0f, 0.0f};
    }

    if (kind == "Text" && text != nullptr) {
        text->text  = "New Text";
        text->align = eng::UIAlign::Center;
        if (rect != nullptr) {
            rect->size = eng::Vec2{200.0f, 32.0f};
        }
    } else if (kind == "Button" && text != nullptr) {
        text->text  = "Button";
        text->align = eng::UIAlign::Center;
        if (rect != nullptr) {
            rect->size = eng::Vec2{160.0f, 40.0f};
        }
    } else if (kind == "Slider" && rect != nullptr) {
        rect->size = eng::Vec2{200.0f, 18.0f};
    } else if (kind == "Image" && rect != nullptr) {
        rect->size = eng::Vec2{120.0f, 120.0f};
    }
}

} // namespace

eng::EntityId CreateObject(std::string_view kind) {
    eng::Scene& scene = eng::Engine::Get().GetScene();

    const ObjectKind* chosen = nullptr;
    for (const ObjectKind& candidate : kKinds) {
        if (kind == candidate.name) {
            chosen = &candidate;
            break;
        }
    }
    if (chosen == nullptr) {
        ENGINE_LOG_WARN(eng::Channels::kEditor, "there is no object kind called '{}'",
                        kind);
        return {};
    }

    const eng::EntityId created = scene.CreateEntity(scene.MakeUniqueName(chosen->name));
    eng::Entity*        entity  = scene.Get(created);
    if (entity == nullptr) {
        return {};
    }

    // Placed where the Scene view is looking, so it appears on screen rather
    // than at the world origin somewhere off in the distance. The same rule
    // the old "+ Create Entity" button used.
    entity->Transform().SetLocalPosition(eng::Engine::Get().GetCamera().Position());

    for (const char* component : chosen->components) {
        if (component == nullptr) {
            break;   // the list is nullptr-terminated
        }
        if (entity->AddComponent(component) == nullptr) {
            ENGINE_LOG_ERROR(eng::Channels::kEditor,
                             "could not add a {} to the new {}", component,
                             chosen->name);
        }
    }

    // AFTER the components exist, because the defaults are set ON them.
    if (chosen->group != nullptr && std::string_view(chosen->group) == "UI") {
        ApplyUIDefaults(chosen->name, *entity);
    }

    return created;
}

eng::EntityId DrawObjectMenuItems() {
    eng::EntityId created;

    // Ungrouped kinds first, at the top level.
    for (const ObjectKind& kind : kKinds) {
        if (kind.group != nullptr) {
            continue;
        }
        if (ImGui::MenuItem(kind.name)) {
            created = CreateObject(kind.name);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", kind.tip);
        }
    }

    // Then the UI submenu. A submenu rather than four more rows because the
    // interface objects are a family - they only make sense together - and
    // because this is where somebody goes looking for them, Unity having put
    // them in the same place.
    if (ImGui::BeginMenu("UI")) {
        for (const ObjectKind& kind : kKinds) {
            if (kind.group == nullptr || std::string_view(kind.group) != "UI") {
                continue;
            }
            if (ImGui::MenuItem(kind.name)) {
                created = CreateObject(kind.name);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", kind.tip);
            }
        }
        ImGui::Separator();
        ImGui::TextDisabled("screen-space by default; switch a");
        ImGui::TextDisabled("UIRect to World to pin it to the scene");
        ImGui::EndMenu();
    }

    // ---- a note about the camera, where it is actually useful -----------
    //
    // Said here rather than only in the docs, because "why is my Game view
    // not moving" is a question somebody asks while looking at this menu.
    ImGui::Separator();

    if (eng::CameraSystem::Count() == 0) {
        ImGui::TextDisabled("no camera in this scene - the view uses the");
        ImGui::TextDisabled("scene's own saved camera position");
    } else if (const eng::CameraComponent* active = eng::CameraSystem::Active()) {
        const eng::Entity* owner = active->Owner();
        ImGui::TextDisabled("view driven by: %s",
                            owner != nullptr ? owner->Name().c_str() : "a camera");
        if (eng::CameraSystem::Count() > 1) {
            ImGui::TextDisabled("(%zu cameras; highest priority wins)",
                                eng::CameraSystem::Count());
        }
    } else {
        ImGui::TextDisabled("%zu camera(s), all disabled", eng::CameraSystem::Count());
    }

    return created;
}

} // namespace editor
