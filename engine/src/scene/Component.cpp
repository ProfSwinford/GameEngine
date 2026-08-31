// WEEK 9 - components, the factory, and the sprite render system.
// See Component.h for the self-registration and AoS/SoA notes.

#include <engine/core/Log.h>
#include <engine/debug/Camera.h>
#include <engine/debug/ScopedTimer.h>
#include <engine/memory/MemorySystem.h>
#include <engine/memory/StackAllocator.h>
#include <engine/scene/Component.h>
#include <engine/scene/Scene.h>

#include <algorithm>
#include <map>

namespace eng {
namespace {

std::map<u64, ComponentFactory::CreateFn>& FactoryTable() {
    static std::map<u64, ComponentFactory::CreateFn> table;
    return table;
}

std::map<u64, std::string>& FactoryNames() {
    static std::map<u64, std::string> names;
    return names;
}

// The dense array the render system walks. See the AoS/SoA note in
// Component.h for why this shape and not a vector of component pointers.
std::vector<SpriteRecord> g_sprites;

} // namespace

// ---------------------------------------------------------------------------
//  Component
// ---------------------------------------------------------------------------

EntityHandle Component::OwnerHandle() const {
    return (m_owner != nullptr) ? m_owner->Handle() : EntityHandle{};
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
    const StringId id = Intern(typeName);
    FactoryTable()[id.Value()] = create;
    FactoryNames()[id.Value()] = std::string(typeName);
    ENGINE_LOG_DEBUG(Channels::kScene, "component type registered: {}", typeName);
}

std::unique_ptr<Component> ComponentFactory::Create(StringId typeId) {
    const auto it = FactoryTable().find(typeId.Value());
    if (it == FactoryTable().end()) {
        return nullptr;
    }
    return it->second();
}

std::unique_ptr<Component> ComponentFactory::Create(std::string_view typeName) {
    return Create(StringId(typeName));
}

bool ComponentFactory::IsRegistered(StringId typeId) {
    return FactoryTable().contains(typeId.Value());
}

void ComponentFactory::ForEachType(const std::function<void(const char*)>& fn) {
    for (const auto& [id, name] : FactoryNames()) {
        fn(name.c_str());
    }
}

// ---------------------------------------------------------------------------
//  TransformComponent
// ---------------------------------------------------------------------------

StringId TransformComponent::TypeIdStatic() {
    // A function-local static so the id is interned exactly once and the
    // reverse table has the text. StringId(kTypeName) alone would be constexpr
    // and free, but then ToString() in a log line would print a hash.
    static const StringId id = Intern(kTypeName);
    return id;
}

bool TransformComponent::Deserialize(const ConfigNode& node, std::string& outError) {
    f32 position[2] = {0.0f, 0.0f};
    if (const ConfigNode child = node.Child("position"); child.IsValid()) {
        if (!child.AsFloatArray(position, 2)) {
            outError = child.Path() + " must be an array of two numbers";
            return false;
        }
    }

    f32 scale[2] = {1.0f, 1.0f};
    if (const ConfigNode child = node.Child("scale"); child.IsValid()) {
        if (!child.AsFloatArray(scale, 2)) {
            outError = child.Path() + " must be an array of two numbers";
            return false;
        }
    }

    const f32 rotation = static_cast<f32>(node.Child("rotation").AsFloat(0.0));

    m_transform.SetLocalPosition(Vec2{position[0], position[1]});
    m_transform.SetLocalRotation(rotation);
    m_transform.SetLocalScale(Vec2{scale[0], scale[1]});
    return true;
}

bool TransformComponent::Serialize(ConfigWriter& out) const {
    // The SAME KEYS Deserialize reads, which is what makes the round trip a
    // fixed point rather than an approximation.
    const Vec2 position = m_transform.LocalPosition();
    const Vec2 scale    = m_transform.LocalScale();

    const f32 positionValues[2] = {position.x, position.y};
    const f32 scaleValues[2]    = {scale.x, scale.y};

    out.SetFloatArray("position", positionValues, 2);
    out.SetFloat("rotation", static_cast<f64>(m_transform.LocalRotation()));
    out.SetFloatArray("scale", scaleValues, 2);
    return true;
}

// ---------------------------------------------------------------------------
//  SpriteComponent
// ---------------------------------------------------------------------------

StringId SpriteComponent::TypeIdStatic() {
    static const StringId id = Intern(kTypeName);
    return id;
}

SpriteComponent::~SpriteComponent() {
    // A safety net, not the mechanism. OnDetach is where the Release belongs
    // and where it normally happens; this catches the case where a component
    // was constructed and destroyed without ever being attached - which
    // happens when Deserialize fails on a half-built entity during load.
    if (!m_texture.IsNull()) {
        ResourceManager::Release(m_texture);
        m_texture = {};
    }
}

bool SpriteComponent::Deserialize(const ConfigNode& node, std::string& outError) {
    const ConfigNode textureNode = node.Child("texture");
    if (!textureNode.IsValid()) {
        outError = node.Path() + " is missing the required field 'texture'";
        return false;
    }
    if (!textureNode.IsString()) {
        outError = textureNode.Path() + " must be a string (a virtual asset path)";
        return false;
    }
    m_texturePath = textureNode.AsString("");

    if (const ConfigNode tint = node.Child("tint"); tint.IsValid()) {
        f32 rgba[4] = {255.0f, 255.0f, 255.0f, 255.0f};
        if (!tint.AsFloatArray(rgba, 4)) {
            outError = tint.Path() + " must be an array of four numbers [r,g,b,a]";
            return false;
        }
        m_tint = Color{static_cast<u8>(std::clamp(rgba[0], 0.0f, 255.0f)),
                       static_cast<u8>(std::clamp(rgba[1], 0.0f, 255.0f)),
                       static_cast<u8>(std::clamp(rgba[2], 0.0f, 255.0f)),
                       static_cast<u8>(std::clamp(rgba[3], 0.0f, 255.0f))};
    }

    m_layer = static_cast<i32>(node.Child("layer").AsInt(0));

    if (const ConfigNode size = node.Child("size"); size.IsValid()) {
        f32 wh[2] = {0.0f, 0.0f};
        if (!size.AsFloatArray(wh, 2)) {
            outError = size.Path() + " must be an array of two numbers [w,h]";
            return false;
        }
        m_pixelSize = Vec2{wh[0], wh[1]};
    }

    return true;
}

bool SpriteComponent::Serialize(ConfigWriter& out) const {
    out.SetString("texture", m_texturePath);

    const f32 tint[4] = {static_cast<f32>(m_tint.r), static_cast<f32>(m_tint.g),
                         static_cast<f32>(m_tint.b), static_cast<f32>(m_tint.a)};
    out.SetFloatArray("tint", tint, 4);
    out.SetInt("layer", m_layer);

    // Only written when it was set. A zero size means "use the texture's own
    // dimensions", and writing [0,0] into every scene file would turn an
    // absent-and-meaningful field into a present-and-confusing one.
    if (m_pixelSize.x > 0.0f && m_pixelSize.y > 0.0f) {
        const f32 size[2] = {m_pixelSize.x, m_pixelSize.y};
        out.SetFloatArray("size", size, 2);
    }
    return true;
}

void SpriteComponent::OnAttach() {
    // ONE Acquire, here. The matching Release is in OnDetach, and both are a
    // single line you can point at - which is the rule that makes refcounting
    // survivable. Acquiring in Deserialize as well would double every count,
    // which is the "refcount higher than expected on load" failure exactly.
    if (!m_texturePath.empty()) {
        m_texture = ResourceManager::AcquireTexture(m_texturePath);
    }
    SpriteRenderSystem::Register(*this);
}

void SpriteComponent::OnDetach() {
    SpriteRenderSystem::Unregister(*this);
    if (!m_texture.IsNull()) {
        ResourceManager::Release(m_texture);
        m_texture = {};
    }
}

void SpriteComponent::SetTint(Color tint) {
    m_tint = tint;
    if (m_recordIndex < g_sprites.size()) {
        g_sprites[m_recordIndex].tint = tint;
    }
}

void SpriteComponent::SetLayer(i32 layer) {
    m_layer = layer;
    if (m_recordIndex < g_sprites.size()) {
        g_sprites[m_recordIndex].layer = layer;
    }
}

void SpriteComponent::SetTexture(std::string_view virtualPath) {
    // Acquire the new one BEFORE releasing the old. If they are the same path,
    // releasing first would take the refcount to zero, unload the texture, and
    // immediately reload it - a visible hitch for a no-op.
    const Handle<Texture> replacement = ResourceManager::AcquireTexture(virtualPath);
    if (!m_texture.IsNull()) {
        ResourceManager::Release(m_texture);
    }
    m_texture     = replacement;
    m_texturePath.assign(virtualPath);

    if (m_recordIndex < g_sprites.size()) {
        g_sprites[m_recordIndex].texture = m_texture;
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

    sprite.m_recordIndex = g_sprites.size();
    g_sprites.push_back(record);
}

void SpriteRenderSystem::Unregister(SpriteComponent& sprite) {
    const usize index = sprite.m_recordIndex;
    if (index >= g_sprites.size()) {
        return;   // never registered, or already removed
    }

    // SWAP AND POP. O(1) removal from a dense array, at the cost of not
    // preserving order - which is fine because the render pass sorts by layer
    // anyway.
    //
    // *** THE FIXUP LINE BELOW IS THE ONE THAT MATTERS. *** The record that
    // was at the back is now at `index`, so its owner's cached index is stale
    // and has to be corrected. Forgetting it means the NEXT Unregister removes
    // the wrong sprite, which presents as a random sprite vanishing when a
    // different one is destroyed - and is exactly why Week 9 evidence question
    // 3 asks what happens to these arrays during iteration.
    const usize last = g_sprites.size() - 1;
    if (index != last) {
        g_sprites[index] = g_sprites[last];
        if (g_sprites[index].owner != nullptr) {
            g_sprites[index].owner->m_recordIndex = index;
        }
    }
    g_sprites.pop_back();
    sprite.m_recordIndex = static_cast<usize>(-1);
}

void SpriteRenderSystem::Render(Camera& camera) {
    ENGINE_SCOPED_TIMER("SpriteRenderSystem::Render");

    if (g_sprites.empty()) {
        return;
    }

    // Sorting an index list rather than the records themselves: moving a
    // 32-byte record would invalidate every owner's cached m_recordIndex, and
    // fixing those up per sort would cost more than the sort.
    //
    // *** WEEK 7 STRETCH 3, DONE HERE: the index list comes from the FRAME
    // STACK ALLOCATOR, not from the heap. ***
    //
    // This is a textbook scratch allocation - it lives for exactly one render
    // pass and dies with every other per-frame temporary when
    // MemorySystem::BeginFrame clears the stack. Allocation is a pointer add,
    // there is no free, and the Memory panel's plot shows the SAWTOOTH that
    // a healthy scratch allocator is supposed to have. Before this change the
    // frame stack read zero all session, which is a suspiciously flat line for
    // an allocator that exists to be used.
    //
    // The heap fallback matters: the stack allocator returns nullptr when it
    // is full rather than growing (which is its contract), and a render pass
    // must not stop drawing because a buffer was sized too small.
    usize*             order    = nullptr;
    StackAllocator*    scratch  = MemorySystem::FrameStack();
    StackAllocator::Marker mark  = 0;
    std::vector<usize> heapOrder;

    if (scratch != nullptr) {
        mark  = scratch->GetMarker();
        order = static_cast<usize*>(
            scratch->Allocate(sizeof(usize) * g_sprites.size(), alignof(usize)));
    }
    if (order == nullptr) {
        heapOrder.resize(g_sprites.size());
        order = heapOrder.data();
    }

    for (usize i = 0; i < g_sprites.size(); ++i) {
        order[i] = i;
    }
    std::stable_sort(order, order + g_sprites.size(), [](usize a, usize b) {
        return g_sprites[a].layer < g_sprites[b].layer;
    });

    for (usize slot = 0; slot < g_sprites.size(); ++slot) {
        const usize index = order[slot];
        const SpriteRecord& record = g_sprites[index];
        if (record.transform == nullptr) {
            continue;
        }

        Texture* texture = ResourceManager::Get(record.texture);
        if (texture == nullptr || texture->state != ResourceState::Ready) {
            continue;   // still loading, stale, or failed - already reported
        }

        // The world matrix the transform already computed. No separate
        // position path for rendering, which is the same discipline the debug
        // draw follows.
        const Mat3 world    = record.transform->WorldMatrix();
        const Vec2 centre   = camera.WorldToScreen(world.GetTranslation());
        const Vec2 scale    = world.GetScale();
        const f32  rotation = world.GetRotation();

        Vec2 sizePixels = record.owner->PixelSize();
        if (sizePixels.x <= 0.0f || sizePixels.y <= 0.0f) {
            sizePixels = Vec2{static_cast<f32>(texture->width),
                              static_cast<f32>(texture->height)};
        }
        const Vec2 onScreen{sizePixels.x * scale.x * camera.Zoom(),
                            sizePixels.y * scale.y * camera.Zoom()};

        Renderer::DrawSprite(record.texture, centre, onScreen, rotation * kRadToDeg,
                             record.tint);
    }

    // Rewind rather than wait for BeginFrame's Clear(). Freeing a thousand
    // objects costs the same as freeing one here - it is a single pointer
    // assignment - and rewinding to the marker means a second system running
    // later in the same frame sees the space as available again. That is the
    // property the stack allocator was built for, used rather than described.
    if (scratch != nullptr && heapOrder.empty()) {
        scratch->FreeToMarker(mark);
    }
}

usize SpriteRenderSystem::Count() {
    return g_sprites.size();
}

void SpriteRenderSystem::Clear() {
    for (SpriteRecord& record : g_sprites) {
        if (record.owner != nullptr) {
            record.owner->m_recordIndex = static_cast<usize>(-1);
        }
    }
    g_sprites.clear();
}

// ---------------------------------------------------------------------------
//  Built-in registration
// ---------------------------------------------------------------------------

void ComponentFactory::RegisterBuiltins() {
    Register(TransformComponent::kTypeName,
             []() -> std::unique_ptr<Component> { return std::make_unique<TransformComponent>(); });
    Register(SpriteComponent::kTypeName,
             []() -> std::unique_ptr<Component> { return std::make_unique<SpriteComponent>(); });
    // Week 10's colliders register themselves from Collider.cpp's
    // RegisterColliderComponents(), called from the same place.
}

} // namespace eng
