// ============================================================================
//  Component.cpp - the base component, the factory, the two built-in component
//  types, and the sprite render system. See Component.h.
// ============================================================================

#include <engine/core/Log.h>
#include <engine/render/Camera.h>
#include <engine/resource/ResourceManager.h>
#include <engine/scene/Component.h>
#include <engine/scene/Scene.h>

#include <algorithm>
#include <map>
#include <numeric>

namespace eng {
namespace {

// The factory table: type name -> a function that makes one.
//
// It lives inside a function rather than as a plain global variable. A global
// is created at an unpredictable point during program start-up, and component
// types may try to register before it exists. A variable inside a function is
// created the first time the function runs, which is by definition before
// anybody can use it.
std::map<std::string, ComponentFactory::CreateFn>& FactoryTable() {
    static std::map<std::string, ComponentFactory::CreateFn> table;
    return table;
}

// The list the render system actually walks. See SpriteRecord in Component.h.
std::vector<SpriteRecord> g_sprites;

} // namespace

// ---------------------------------------------------------------------------
//  Component
// ---------------------------------------------------------------------------

EntityId Component::OwnerId() const {
    return (m_owner != nullptr) ? m_owner->Id() : EntityId{};
}

Scene* Component::GetScene() const {
    return (m_owner != nullptr) ? m_owner->GetScene() : nullptr;
}

Transform2D* Component::OwnerTransform() const {
    return (m_owner != nullptr) ? &m_owner->Transform() : nullptr;
}

// ---------------------------------------------------------------------------
//  ComponentFactory
// ---------------------------------------------------------------------------

void ComponentFactory::Register(std::string_view typeName, CreateFn create) {
    FactoryTable()[std::string(typeName)] = create;
}

std::unique_ptr<Component> ComponentFactory::Create(std::string_view typeName) {
    const auto it = FactoryTable().find(std::string(typeName));
    if (it == FactoryTable().end()) {
        return nullptr;
    }
    return it->second();   // calls the stored function, which builds one
}

bool ComponentFactory::IsRegistered(std::string_view typeName) {
    return FactoryTable().contains(std::string(typeName));
}

void ComponentFactory::ForEachType(const std::function<void(const char*)>& fn) {
    for (const auto& [name, create] : FactoryTable()) {
        fn(name.c_str());
    }
}

// ---------------------------------------------------------------------------
//  TransformComponent
// ---------------------------------------------------------------------------

bool TransformComponent::Deserialize(const Json& node, std::string& outError) {
    // Every field is optional and falls back to a sensible value, so a scene
    // file can say "position" only and leave rotation and scale alone.
    m_transform.SetLocalPosition(ReadVec2(node, "position", Vec2{0.0f, 0.0f},
                                          kTypeName));
    m_transform.SetLocalRotation(ReadFloat(node, "rotation", 0.0f, kTypeName));
    m_transform.SetLocalScale(ReadVec2(node, "scale", Vec2{1.0f, 1.0f}, kTypeName));

    outError.clear();
    return true;
}

bool TransformComponent::Serialize(Json& out) const {
    // The SAME KEY NAMES Deserialize reads. That is what makes
    // load -> edit -> save -> load give back exactly what you had.
    WriteVec2(out, "position", m_transform.LocalPosition());
    out["rotation"] = m_transform.LocalRotation();
    WriteVec2(out, "scale", m_transform.LocalScale());
    return true;
}

// ---------------------------------------------------------------------------
//  SpriteComponent
// ---------------------------------------------------------------------------

SpriteComponent::~SpriteComponent() {
    // Nothing to release by hand. m_texture is a shared_ptr, so it lets go of
    // its share of the texture automatically here - and if this was the last
    // sprite using that picture, the picture unloads itself.
}

bool SpriteComponent::Deserialize(const Json& node, std::string& outError) {
    if (!HasKey(node, "texture")) {
        outError = "SpriteComponent needs a \"texture\" field naming an image file";
        return false;
    }
    m_texturePath = ReadString(node, "texture", "", kTypeName);
    if (m_texturePath.empty()) {
        outError = "SpriteComponent's \"texture\" must be text, e.g. "
                   "\"textures/player.bmp\"";
        return false;
    }

    // Tint is four numbers from 0 to 255. Written that way rather than as
    // 0.0-1.0 because it matches what an image editor's colour picker shows.
    if (HasKey(node, "tint")) {
        const Json& tint = node["tint"];
        if (tint.is_array() && tint.size() == 4) {
            auto channel = [](const Json& value) {
                return static_cast<unsigned char>(
                    std::clamp(value.is_number() ? value.get<float>() : 255.0f,
                               0.0f, 255.0f));
            };
            m_tint = Color{channel(tint[0]), channel(tint[1]), channel(tint[2]),
                           channel(tint[3])};
        } else {
            ENGINE_LOG_WARN(Channels::kScene,
                            "SpriteComponent.tint should be four numbers like "
                            "[255, 255, 255, 255]; using white");
        }
    }

    m_layer     = ReadInt(node, "layer", 0, kTypeName);
    m_pixelSize = ReadVec2(node, "size", Vec2{0.0f, 0.0f}, kTypeName);

    outError.clear();
    return true;
}

bool SpriteComponent::Serialize(Json& out) const {
    out["texture"] = m_texturePath;

    Json tint = Json::array();
    tint.push_back(static_cast<int>(m_tint.r));
    tint.push_back(static_cast<int>(m_tint.g));
    tint.push_back(static_cast<int>(m_tint.b));
    tint.push_back(static_cast<int>(m_tint.a));
    out["tint"] = std::move(tint);

    out["layer"] = m_layer;

    // Only written when it was actually set. A size of (0,0) means "use the
    // image's own size", and putting [0,0] into every scene file would turn a
    // meaningfully-absent field into a confusingly-present one.
    if (m_pixelSize.x > 0.0f && m_pixelSize.y > 0.0f) {
        WriteVec2(out, "size", m_pixelSize);
    }
    return true;
}

void SpriteComponent::OnAttach() {
    if (!m_texturePath.empty()) {
        m_texture = ResourceManager::LoadTexture(m_texturePath);
    }
    SpriteRenderSystem::Register(*this);
}

void SpriteComponent::OnDetach() {
    SpriteRenderSystem::Unregister(*this);

    // reset() lets go of this component's share of the texture. If no other
    // sprite is using that image, it unloads here.
    m_texture.reset();
}

void SpriteComponent::SetTint(Color tint) {
    m_tint = tint;
    // The render system keeps its own copy, so it has to be updated too.
    if (m_recordIndex >= 0 && m_recordIndex < static_cast<int>(g_sprites.size())) {
        g_sprites[static_cast<std::size_t>(m_recordIndex)].tint = tint;
    }
}

void SpriteComponent::SetLayer(int layer) {
    m_layer = layer;
    if (m_recordIndex >= 0 && m_recordIndex < static_cast<int>(g_sprites.size())) {
        g_sprites[static_cast<std::size_t>(m_recordIndex)].layer = layer;
    }
}

void SpriteComponent::SetTexture(std::string_view virtualPath) {
    m_texture = ResourceManager::LoadTexture(virtualPath);
    m_texturePath.assign(virtualPath);

    if (m_recordIndex >= 0 && m_recordIndex < static_cast<int>(g_sprites.size())) {
        g_sprites[static_cast<std::size_t>(m_recordIndex)].texture = m_texture;
    }
}

// ---------------------------------------------------------------------------
//  SpriteRenderSystem
// ---------------------------------------------------------------------------

void SpriteRenderSystem::Register(SpriteComponent& sprite) {
    SpriteRecord record;
    record.transform = sprite.OwnerTransform();
    record.texture   = sprite.GetTexture();
    record.tint      = sprite.Tint();
    record.layer     = sprite.Layer();
    record.owner     = &sprite;

    sprite.m_recordIndex = static_cast<int>(g_sprites.size());
    g_sprites.push_back(std::move(record));
}

void SpriteRenderSystem::Unregister(SpriteComponent& sprite) {
    const int index = sprite.m_recordIndex;
    if (index < 0 || index >= static_cast<int>(g_sprites.size())) {
        return;   // never registered, or already removed
    }

    // "Swap and pop": move the LAST entry into the hole and shorten the list.
    // Removing from the middle of a vector the normal way shuffles everything
    // after it along; this is one move regardless of size. The order of the
    // list does not matter because the draw loop sorts by layer anyway.
    const std::size_t slot = static_cast<std::size_t>(index);
    const std::size_t last = g_sprites.size() - 1;
    if (slot != last) {
        g_sprites[slot] = std::move(g_sprites[last]);

        // THE LINE THAT IS EASY TO FORGET. The entry that just moved belongs
        // to some other sprite, and that sprite is still remembering its OLD
        // position. Without this fix-up, the next removal takes out the wrong
        // sprite - which shows up as a random unrelated sprite vanishing.
        if (g_sprites[slot].owner != nullptr) {
            g_sprites[slot].owner->m_recordIndex = static_cast<int>(slot);
        }
    }
    g_sprites.pop_back();
    sprite.m_recordIndex = -1;
}

void SpriteRenderSystem::Render(Camera& camera) {
    if (g_sprites.empty()) {
        return;
    }

    // Sprites have to be drawn back to front, but the records themselves must
    // not be reordered - every sprite remembers where its own record is. So a
    // list of POSITIONS is sorted instead, and the records stay put.
    std::vector<std::size_t> order(g_sprites.size());

    // std::iota fills a range with 0, 1, 2, 3... which is the starting order.
    std::iota(order.begin(), order.end(), std::size_t{0});

    // stable_sort rather than sort: two sprites on the same layer keep the
    // order they were added in, so the picture does not flicker between frames.
    std::stable_sort(order.begin(), order.end(), [](std::size_t a, std::size_t b) {
        return g_sprites[a].layer < g_sprites[b].layer;
    });

    for (const std::size_t index : order) {
        const SpriteRecord& record = g_sprites[index];
        if (record.transform == nullptr || !record.texture) {
            continue;   // no position, or no picture assigned yet
        }

        // The transform's own world matrix is used here - the same one
        // everything else in the engine uses. There is deliberately no second
        // piece of code working out where a sprite goes, because two such
        // pieces would eventually disagree.
        const Mat3  world    = record.transform->WorldMatrix();
        const Vec2  centre   = camera.WorldToScreen(world.GetTranslation());
        const Vec2  scale    = world.GetScale();
        const float rotation = world.GetRotation();

        Vec2 sizePixels = record.owner->PixelSize();
        if (sizePixels.x <= 0.0f || sizePixels.y <= 0.0f) {
            sizePixels = Vec2{static_cast<float>(record.texture->width),
                              static_cast<float>(record.texture->height)};
        }

        const Vec2 onScreen{sizePixels.x * scale.x * camera.Zoom(),
                            sizePixels.y * scale.y * camera.Zoom()};

        // SDL turns things clockwise and measures in degrees; the engine works
        // anticlockwise in radians. The camera's y flip already accounts for
        // the direction, so only the unit conversion is needed here.
        Renderer::DrawSprite(record.texture, centre, onScreen, rotation * kRadToDeg,
                             record.tint);
    }
}

std::size_t SpriteRenderSystem::Count() {
    return g_sprites.size();
}

void SpriteRenderSystem::Clear() {
    for (SpriteRecord& record : g_sprites) {
        if (record.owner != nullptr) {
            record.owner->m_recordIndex = -1;
        }
    }
    g_sprites.clear();
}

// ---------------------------------------------------------------------------
//  Built-in component types
// ---------------------------------------------------------------------------

void ComponentFactory::RegisterBuiltins() {
    // The lambda after each name is the "how to make one" function. It has no
    // captures (nothing inside the []), which is what allows it to be stored
    // as a plain function pointer.
    Register(TransformComponent::kTypeName, []() -> std::unique_ptr<Component> {
        return std::make_unique<TransformComponent>();
    });
    Register(SpriteComponent::kTypeName, []() -> std::unique_ptr<Component> {
        return std::make_unique<SpriteComponent>();
    });
    // Colliders, spin and scripts register themselves from their own files,
    // called from the same place at start-up.
}

} // namespace eng
