// ============================================================================
//  Prefab.cpp - the prefab asset format, the diff that finds overrides, and
//  the four things a person does with an instance. See Prefab.h for why any
//  of it is shaped this way.
//
//  WHAT IS NOT IN THIS FILE: the lifetime of an instance's entities. The scene
//  owns those, because the scene owns every entity - see the prefab section of
//  Scene.cpp. This file owns the FORMAT and the COMPARISON; that one does the
//  creating and destroying. Keeping the split means the diff can be read
//  without knowing about slots and generations, and the slot bookkeeping can
//  be read without knowing what a dotted field path is.
// ============================================================================

#include <engine/scene/Prefab.h>

#include <engine/core/Log.h>
#include <engine/fs/FileSystem.h>
#include <engine/math/Transform2D.h>
#include <engine/scene/Component.h>
#include <engine/scene/Entity.h>
#include <engine/scene/Scene.h>

#include <algorithm>
#include <charconv>
#include <map>
#include <unordered_set>

namespace eng {
namespace {

// The same reader Scene.cpp uses, and for the same reason: square brackets
// INSERT a missing key, so `document["entities"]` would modify the document
// just by asking what is in it.
const Json& Field(const Json& object, const char* name) {
    static const Json kEmpty = Json::object();
    if (!object.is_object()) {
        return kEmpty;
    }
    const auto it = object.find(name);
    return (it != object.end()) ? *it : kEmpty;
}

std::string TypeOf(const Json& componentNode) {
    return ReadString(componentNode, "type", "");
}

// Where the `slot`th component of this type sits in a components array, or -1.
//
// SLOT RATHER THAN INDEX is what lets an entity carry three scripts and still
// have a stable way to name the middle one. The position in the array moves
// whenever anything before it is added or removed; "the second
// ScriptComponent" does not.
int ComponentIndex(const Json& components, const std::string& type, int slot) {
    if (!components.is_array()) {
        return -1;
    }
    int seen = 0;
    for (std::size_t i = 0; i < components.size(); ++i) {
        if (TypeOf(components[i]) != type) {
            continue;
        }
        if (seen == slot) {
            return static_cast<int>(i);
        }
        ++seen;
    }
    return -1;
}

// ---------------------------------------------------------------------------
//  DOTTED FIELD PATHS
//
//  "tint" is one field. "vars.damage" is one field inside a nested object.
//  Arrays are never split - see the note at the top of Prefab.h about why
//  half a position is not a thing anybody wants to override.
// ---------------------------------------------------------------------------

std::vector<std::string> SplitPath(std::string_view path) {
    std::vector<std::string> parts;
    std::size_t              start = 0;
    while (true) {
        const std::size_t dot = path.find('.', start);
        if (dot == std::string_view::npos) {
            parts.emplace_back(path.substr(start));
            return parts;
        }
        parts.emplace_back(path.substr(start, dot - start));
        start = dot + 1;
    }
}

// Every leaf field in a component node, as a dotted path.
//
// "type" IS SKIPPED, and only at the top level. It is the component's
// identity, not one of its settings: an override that changed it would turn
// the component into a different kind of component, which is an add and a
// remove rather than an edit.
void ForEachField(const Json& node, const std::string& prefix,
                  const std::function<void(const std::string&)>& fn) {
    if (!node.is_object()) {
        return;
    }
    for (const auto& entry : node.items()) {
        const std::string& key = entry.key();
        if (prefix.empty() && key == "type") {
            continue;
        }
        const std::string path = prefix.empty() ? key : prefix + "." + key;
        if (entry.value().is_object()) {
            ForEachField(entry.value(), path, fn);
        } else {
            fn(path);
        }
    }
}

const Json* FindField(const Json& node, std::string_view path) {
    const Json* current = &node;
    for (const std::string& part : SplitPath(path)) {
        if (!current->is_object()) {
            return nullptr;
        }
        const auto it = current->find(part);
        if (it == current->end()) {
            return nullptr;
        }
        current = &(*it);
    }
    return current;
}

void SetField(Json& node, std::string_view path, const Json& value) {
    const std::vector<std::string> parts = SplitPath(path);
    Json*                          current = &node;
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        // A hand-edited asset can have a number where this expects an object,
        // and nlohmann THROWS when asked to index one. Replaced rather than
        // left to throw: the override is what the person asked for, and the
        // asset being malformed there is not a reason to take the editor down.
        if (!current->is_object()) {
            *current = Json::object();
        }
        current = &(*current)[parts[i]];
    }
    if (!current->is_object()) {
        *current = Json::object();
    }
    (*current)[parts.back()] = value;
}

void EraseField(Json& node, std::string_view path) {
    const std::vector<std::string> parts = SplitPath(path);
    Json*                          current = &node;
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        if (!current->is_object()) {
            return;
        }
        const auto it = current->find(parts[i]);
        if (it == current->end()) {
            return;
        }
        current = &(*it);
    }
    if (current->is_object()) {
        current->erase(parts.back());
    }
}

// ---------------------------------------------------------------------------
//  THE DIFF, for one component
// ---------------------------------------------------------------------------
void DiffComponent(const Json& assetNode, const Json& liveNode, int localId,
                   const std::string& type, int slot,
                   std::vector<PrefabOverride>& out) {
    // Every field either side knows about, in asset order then live order, so
    // the result is deterministic and a saved scene does not churn.
    std::vector<std::string> paths;
    const auto               collect = [&paths](const std::string& path) {
        if (std::find(paths.begin(), paths.end(), path) == paths.end()) {
            paths.push_back(path);
        }
    };
    ForEachField(assetNode, {}, collect);
    ForEachField(liveNode, {}, collect);

    for (const std::string& path : paths) {
        const Json* assetValue = FindField(assetNode, path);
        const Json* liveValue  = FindField(liveNode, path);

        if (liveValue == nullptr) {
            // ==============================================================
            //  THE ASSET HAS THIS FIELD AND THE INSTANCE DOES NOT, which is
            //  recorded as an override whose value is JSON null - and merging
            //  one of those back ERASES the key rather than writing a null
            //  into it.
            //
            //  It happens when a component only writes a field sometimes:
            //  ScriptComponent leaves "vars" out entirely when there is
            //  nothing in it. Without this the asset's value would come back
            //  on every rebuild and the instance could never be rid of it.
            //
            //  NO COMPONENT IN THIS ENGINE WRITES A REAL null, which is what
            //  makes null safe to use as the marker.
            // ==============================================================
            out.push_back(PrefabOverride{localId, type, slot, path, Json()});
            continue;
        }
        if (assetValue == nullptr || *assetValue != *liveValue) {
            out.push_back(PrefabOverride{localId, type, slot, path, *liveValue});
        }
    }
}

// Serialises a live component the way the scene file would, "type" included.
// Returns false for a component that cannot be saved at all.
bool SerializeComponent(Component& component, Json& out) {
    out = Json::object();
    if (!component.Serialize(out)) {
        return false;
    }
    // Written HERE rather than by the component, exactly as
    // BuildSceneDocument does it, so no component can get its own name wrong.
    out["type"] = component.TypeName();
    return true;
}

// Which entity owns which transform. The transform tree knows about parents;
// the scene's list of entities is flat, so this is the only way across.
std::unordered_map<const Transform2D*, Entity*> OwnersByTransform(Scene& scene) {
    std::unordered_map<const Transform2D*, Entity*> owners;
    scene.ForEach([&owners](Entity& entity) { owners[&entity.Transform()] = &entity; });
    return owners;
}

bool ParseLocalId(const std::string& text, int& out) {
    const char* begin = text.data();
    const char* end   = text.data() + text.size();
    const auto  result = std::from_chars(begin, end, out);
    return result.ec == std::errc{} && result.ptr == end;
}

// The name the asset gives one of its entities, for the override descriptions.
std::string AssetEntityName(const PrefabAsset& asset, int localId) {
    if (const PrefabEntityNode* node = asset.Find(localId)) {
        const std::string name = ReadString(node->node, "name", "");
        if (!name.empty()) {
            return name;
        }
    }
    return "entity " + std::to_string(localId);
}

// "SpriteComponent" or "SpriteComponent [2]" when several of a type sit on one
// entity, because "which of the three scripts?" is the whole question there.
std::string DescribeComponent(const std::string& type, int slot) {
    if (slot == 0) {
        return type;
    }
    return type + " [" + std::to_string(slot + 1) + "]";
}

} // namespace

// ---------------------------------------------------------------------------
//  PrefabAsset
// ---------------------------------------------------------------------------

const PrefabEntityNode* PrefabAsset::Find(int localId) const {
    for (const PrefabEntityNode& entry : m_entities) {
        if (entry.localId == localId) {
            return &entry;
        }
    }
    return nullptr;
}

int PrefabAsset::NextFreeId() const {
    int highest = 0;
    for (const PrefabEntityNode& entry : m_entities) {
        highest = std::max(highest, entry.localId);
    }
    return highest + 1;
}

bool PrefabAsset::Parse(const Json& document, std::string& outError) {
    m_document = document;
    m_entities.clear();
    m_rootId = 0;
    m_name.clear();

    if (!document.is_object()) {
        outError = "a prefab file has to be a JSON object";
        return false;
    }

    m_name = ReadString(document, "name", "Prefab");

    const Json& entities = Field(document, "entities");
    if (!entities.is_array() || entities.empty()) {
        outError = "this prefab has no \"entities\" list";
        return false;
    }

    std::unordered_set<int> used;
    for (const Json& node : entities) {
        if (!node.is_object()) {
            outError = "every entry in a prefab's \"entities\" list has to be an object";
            return false;
        }
        const int localId = ReadInt(node, "id", 0);
        if (localId <= 0) {
            outError = "every entity in a prefab needs an \"id\" of 1 or more - that "
                       "number is what an override refers to";
            return false;
        }
        if (!used.insert(localId).second) {
            outError = "two entities in this prefab share the id " +
                       std::to_string(localId) +
                       ", so an override could not say which one it meant";
            return false;
        }

        PrefabEntityNode entry;
        entry.localId = localId;
        entry.parent  = ReadInt(node, "parent", 0);
        entry.node    = node;
        m_entities.push_back(std::move(entry));
    }

    // "root" may be left out, in which case the first entity is it - which is
    // what a hand-written prefab will almost always mean.
    m_rootId = ReadInt(document, "root", m_entities.front().localId);
    if (Find(m_rootId) == nullptr) {
        outError = "this prefab's \"root\" is " + std::to_string(m_rootId) +
                   ", which is not one of its entities";
        return false;
    }

    for (PrefabEntityNode& entry : m_entities) {
        if (entry.parent == entry.localId) {
            ENGINE_LOG_WARN(Channels::kScene,
                            "prefab '{}': entity {} says it is its own parent; treated "
                            "as top level", m_name, entry.localId);
            entry.parent = 0;
        } else if (entry.parent != 0 && Find(entry.parent) == nullptr) {
            ENGINE_LOG_WARN(Channels::kScene,
                            "prefab '{}': entity {} names parent {}, which is not in "
                            "this prefab; treated as top level", m_name, entry.localId,
                            entry.parent);
            entry.parent = 0;
        }
    }

    // A LONGER LOOP IN THE PARENT CHAIN IS NOT CHECKED HERE, and does not need
    // to be: parents are set with Transform2D::SetParent, which refuses to
    // make anything its own ancestor and says so. See the second rule at the
    // top of Transform2D.h.

    outError.clear();
    return true;
}

// ---------------------------------------------------------------------------
//  PrefabLibrary
// ---------------------------------------------------------------------------

namespace {

// Function-local rather than a file-level global, so it is built the first
// time it is asked for. A file-level one in a shared library is built before
// main and torn down after it, which is exactly when the logger is not there.
std::unordered_map<std::string, PrefabAsset>& Cache() {
    static std::unordered_map<std::string, PrefabAsset> cache;
    return cache;
}

} // namespace

const PrefabAsset* PrefabLibrary::Load(std::string_view virtualPath, std::string& outError) {
    const std::string key(virtualPath);
    if (key.empty()) {
        outError = "no prefab was named";
        return nullptr;
    }

    if (const auto it = Cache().find(key); it != Cache().end()) {
        outError.clear();
        return &it->second;
    }

    std::string text;
    if (!FileSystem::ReadTextFile(key, text, outError)) {
        return nullptr;
    }

    std::string parseError;
    const Json  document = ParseJson(text, parseError);
    if (!parseError.empty()) {
        // ParseJson's message says WHERE in the file, which is the only part
        // that helps when you have the file open.
        outError = key + ": " + parseError;
        return nullptr;
    }

    PrefabAsset asset;
    if (!asset.Parse(document, outError)) {
        outError = key + ": " + outError;
        return nullptr;
    }

    // ON SUCCESS ONLY. A missing or broken prefab must start working the
    // moment the file is fixed, without restarting the editor - see the note
    // in the header.
    const auto inserted = Cache().emplace(std::move(key), std::move(asset));
    outError.clear();
    return &inserted.first->second;
}

void PrefabLibrary::Forget(std::string_view virtualPath) {
    Cache().erase(std::string(virtualPath));
}

void PrefabLibrary::Clear() {
    Cache().clear();
}

std::size_t PrefabLibrary::Count() {
    return Cache().size();
}

void PrefabLibrary::ForEachLoaded(const std::function<void(const std::string&)>& fn) {
    // A COPY OF THE KEYS FIRST. The caller's whole reason for asking is to
    // decide what to reload, and reloading calls Forget - which erases from
    // the very container being walked.
    std::vector<std::string> paths;
    paths.reserve(Cache().size());
    for (const auto& entry : Cache()) {
        paths.push_back(entry.first);
    }
    for (const std::string& path : paths) {
        fn(path);
    }
}

// ---------------------------------------------------------------------------
//  THE FILE FORMAT
// ---------------------------------------------------------------------------

Json WriteInstanceToJson(const PrefabInstance& instance) {
    Json out      = Json::object();
    out["prefab"] = instance.prefabPath;
    out["name"]   = instance.rootName;
    if (!instance.parentName.empty()) {
        out["parent"] = instance.parentName;
    }

    if (!instance.names.empty()) {
        // A std::map, so the keys come out in the same order every time. An
        // unordered_map would write them in whatever order its buckets happen
        // to be in, and every save of an unchanged scene would look like a
        // change to version control.
        const std::map<int, std::string> ordered(instance.names.begin(),
                                                 instance.names.end());
        Json                             names = Json::object();
        for (const auto& entry : ordered) {
            names[std::to_string(entry.first)] = entry.second;
        }
        out["names"] = std::move(names);
    }

    // SORTED, for the same reason. The diff walks live components in
    // attachment order and entities in slot order, neither of which is stable
    // across a reload.
    std::vector<PrefabOverride> overrides = instance.overrides;
    std::sort(overrides.begin(), overrides.end(),
              [](const PrefabOverride& a, const PrefabOverride& b) {
                  if (a.localId != b.localId) {
                      return a.localId < b.localId;
                  }
                  if (a.component != b.component) {
                      return a.component < b.component;
                  }
                  if (a.slot != b.slot) {
                      return a.slot < b.slot;
                  }
                  return a.field < b.field;
              });

    if (!overrides.empty()) {
        Json list = Json::array();
        for (const PrefabOverride& item : overrides) {
            Json node         = Json::object();
            node["entity"]    = item.localId;
            node["component"] = item.component;
            if (item.slot != 0) {
                node["slot"] = item.slot;
            }
            node["field"] = item.field;
            node["value"] = item.value;
            list.push_back(std::move(node));
        }
        out["overrides"] = std::move(list);
    }

    if (!instance.addedComponents.empty()) {
        std::vector<PrefabAddedComponent> added = instance.addedComponents;
        std::sort(added.begin(), added.end(),
                  [](const PrefabAddedComponent& a, const PrefabAddedComponent& b) {
                      if (a.localId != b.localId) {
                          return a.localId < b.localId;
                      }
                      return TypeOf(a.component) < TypeOf(b.component);
                  });
        Json list = Json::array();
        for (const PrefabAddedComponent& item : added) {
            Json node            = Json::object();
            node["entity"]       = item.localId;
            node["component"]    = item.component;
            list.push_back(std::move(node));
        }
        out["addedComponents"] = std::move(list);
    }

    if (!instance.removedComponents.empty()) {
        std::vector<PrefabRemovedComponent> removed = instance.removedComponents;
        std::sort(removed.begin(), removed.end(),
                  [](const PrefabRemovedComponent& a, const PrefabRemovedComponent& b) {
                      if (a.localId != b.localId) {
                          return a.localId < b.localId;
                      }
                      if (a.component != b.component) {
                          return a.component < b.component;
                      }
                      return a.slot < b.slot;
                  });
        Json list = Json::array();
        for (const PrefabRemovedComponent& item : removed) {
            Json node         = Json::object();
            node["entity"]    = item.localId;
            node["component"] = item.component;
            if (item.slot != 0) {
                node["slot"] = item.slot;
            }
            list.push_back(std::move(node));
        }
        out["removedComponents"] = std::move(list);
    }

    return out;
}

bool ReadInstanceFromJson(const Json& node, PrefabInstance& outInstance,
                          std::string& outError) {
    outInstance = PrefabInstance{};

    outInstance.prefabPath = ReadString(node, "prefab", "");
    if (outInstance.prefabPath.empty()) {
        outError = "a prefab instance in this scene has no \"prefab\" path, so there "
                   "is no way to know what it is an instance of";
        return false;
    }
    outInstance.rootName   = ReadString(node, "name", "");
    outInstance.parentName = ReadString(node, "parent", "");

    const Json& names = Field(node, "names");
    if (names.is_object()) {
        for (const auto& entry : names.items()) {
            int localId = 0;
            if (!ParseLocalId(entry.key(), localId) || !entry.value().is_string()) {
                ENGINE_LOG_WARN(Channels::kScene,
                                "prefab instance '{}': \"names\" has an entry keyed "
                                "'{}', which is not an entity id; skipped",
                                outInstance.rootName, entry.key());
                continue;
            }
            outInstance.names[localId] = entry.value().get<std::string>();
        }
    }

    // ---- the overrides ----------------------------------------------------
    //
    // AN ENTRY THAT CANNOT BE UNDERSTOOD IS SKIPPED AND REPORTED, not treated
    // as a failure. A scene saved by a later build may hold kinds of override
    // this one has never heard of, and refusing to open it would turn a new
    // feature into a file nobody can read.
    const Json& overrides = Field(node, "overrides");
    if (overrides.is_array()) {
        for (const Json& item : overrides) {
            PrefabOverride record;
            record.localId   = ReadInt(item, "entity", 0);
            record.component = ReadString(item, "component", "");
            record.slot      = ReadInt(item, "slot", 0);
            record.field     = ReadString(item, "field", "");
            if (record.localId <= 0 || record.component.empty() || record.field.empty()) {
                ENGINE_LOG_WARN(Channels::kScene,
                                "prefab instance '{}': an override is missing its "
                                "\"entity\", \"component\" or \"field\"; skipped",
                                outInstance.rootName);
                continue;
            }
            if (HasKey(item, "value")) {
                record.value = item.at("value");
            }
            outInstance.overrides.push_back(std::move(record));
        }
    }

    const Json& added = Field(node, "addedComponents");
    if (added.is_array()) {
        for (const Json& item : added) {
            PrefabAddedComponent record;
            record.localId   = ReadInt(item, "entity", 0);
            record.component = Field(item, "component");
            if (record.localId <= 0 || TypeOf(record.component).empty()) {
                ENGINE_LOG_WARN(Channels::kScene,
                                "prefab instance '{}': an added component has no "
                                "\"entity\" or no component \"type\"; skipped",
                                outInstance.rootName);
                continue;
            }
            outInstance.addedComponents.push_back(std::move(record));
        }
    }

    const Json& removed = Field(node, "removedComponents");
    if (removed.is_array()) {
        for (const Json& item : removed) {
            PrefabRemovedComponent record;
            record.localId   = ReadInt(item, "entity", 0);
            record.component = ReadString(item, "component", "");
            record.slot      = ReadInt(item, "slot", 0);
            if (record.localId <= 0 || record.component.empty()) {
                ENGINE_LOG_WARN(Channels::kScene,
                                "prefab instance '{}': a removed component has no "
                                "\"entity\" or no \"component\"; skipped",
                                outInstance.rootName);
                continue;
            }
            outInstance.removedComponents.push_back(std::move(record));
        }
    }

    outError.clear();
    return true;
}

// ---------------------------------------------------------------------------
//  BUILDING AND COMPARING
// ---------------------------------------------------------------------------

std::vector<Json> EffectiveEntityNodes(const PrefabAsset& asset,
                                       const PrefabInstance& instance) {
    std::vector<Json> out;
    out.reserve(asset.Entities().size());

    for (const PrefabEntityNode& entry : asset.Entities()) {
        Json node = entry.node;

        if (const auto it = instance.names.find(entry.localId);
            it != instance.names.end()) {
            node["name"] = it->second;
        }

        Json& components = node["components"];
        if (!components.is_array()) {
            components = Json::array();
        }

        // ==================================================================
        //  OVERRIDE FIRST, THEN REMOVE, THEN ADD. The order is load-bearing.
        //
        //  An override names its component by slot - "the second
        //  ScriptComponent" - and those slots are counted against the ASSET's
        //  list, because that is the list the diff compared against. Removing
        //  anything first would renumber them, so an instance that both
        //  removed one script and edited another would apply the edit to the
        //  wrong one.
        //
        //  Adding last is what makes an added component's slot predictable:
        //  it goes on the end, which is where the diff will look for it next
        //  time round.
        // ==================================================================
        for (const PrefabOverride& item : instance.overrides) {
            if (item.localId != entry.localId) {
                continue;
            }
            const int index = ComponentIndex(components, item.component, item.slot);
            if (index < 0) {
                // An override written against a version of the asset that no
                // longer has this component. Dropped silently: it is the
                // ordinary consequence of somebody editing the prefab, and a
                // warning per instance per load would bury the Console.
                continue;
            }
            if (item.value.is_null()) {
                EraseField(components[static_cast<std::size_t>(index)], item.field);
            } else {
                SetField(components[static_cast<std::size_t>(index)], item.field,
                         item.value);
            }
        }

        for (const PrefabRemovedComponent& item : instance.removedComponents) {
            if (item.localId != entry.localId) {
                continue;
            }
            const int index = ComponentIndex(components, item.component, item.slot);
            if (index >= 0) {
                components.erase(components.begin() + index);
            }
        }

        for (const PrefabAddedComponent& item : instance.addedComponents) {
            if (item.localId != entry.localId) {
                continue;
            }
            components.push_back(item.component);
        }

        out.push_back(std::move(node));
    }

    return out;
}

void RefreshPrefabPlacement(Scene& scene, PrefabInstance& instance) {
    const EntityId rootId = scene.PrefabRootOf(instance);
    Entity*        root   = scene.Get(rootId);
    if (root == nullptr) {
        return;
    }

    // RENAMING AN INSTANCE IS NOT AN OVERRIDE, it is just its name - so the
    // record follows it rather than recording a difference. Same for the
    // parent: dragging an instance onto something else in the Hierarchy moves
    // the instance, it does not modify the prefab.
    instance.rootName = root->Name();

    instance.parentName.clear();
    const Transform2D* parent = root->Transform().Parent();
    if (parent == nullptr) {
        return;
    }
    const std::unordered_map<const Transform2D*, Entity*> owners = OwnersByTransform(scene);
    if (const auto it = owners.find(parent); it != owners.end()) {
        instance.parentName = it->second->Name();
    }
}

void CapturePrefabOverrides(Scene& scene, const PrefabAsset& asset,
                            PrefabInstance& instance, std::size_t* outUnsaveable) {
    instance.overrides.clear();
    instance.addedComponents.clear();
    instance.removedComponents.clear();

    RefreshPrefabPlacement(scene, instance);

    scene.ForEachPrefabMember(instance, [&](Entity& entity, int localId) {
        const PrefabEntityNode* assetNode = asset.Find(localId);
        if (assetNode == nullptr) {
            // A member whose local id the asset no longer has. Nothing to
            // compare it against, so nothing is recorded; the next rebuild
            // will not create it again.
            return;
        }
        const Json& assetComponents = Field(assetNode->node, "components");

        // ---- the live components, matched to the asset's by (type, slot) ---
        std::unordered_map<std::string, int> liveCount;
        for (std::size_t i = 0; i < entity.ComponentCount(); ++i) {
            Component* componentPtr = entity.ComponentAt(i);
            if (componentPtr == nullptr) {
                continue;
            }
            const std::string type = componentPtr->TypeName();
            const int         slot = liveCount[type]++;

            Json live;
            if (!SerializeComponent(*componentPtr, live)) {
                // It cannot be written down, so no override on it can be
                // either. Counted for the caller, because a Play-mode
                // snapshot that cannot record an override would silently
                // revert it when Stop restored the scene - the same argument
                // Scene::SaveToString makes about a missing component.
                if (outUnsaveable != nullptr) {
                    ++*outUnsaveable;
                }
                continue;
            }

            const int assetIndex = ComponentIndex(assetComponents, type, slot);
            if (assetIndex < 0) {
                instance.addedComponents.push_back(
                    PrefabAddedComponent{localId, std::move(live)});
                continue;
            }
            DiffComponent(assetComponents[static_cast<std::size_t>(assetIndex)], live,
                          localId, type, slot, instance.overrides);
        }

        // ---- and the asset's components with nothing live to match ---------
        if (!assetComponents.is_array()) {
            return;
        }
        std::unordered_map<std::string, int> assetCount;
        for (const Json& componentNode : assetComponents) {
            const std::string type = TypeOf(componentNode);
            if (type.empty()) {
                continue;
            }
            const int slot = assetCount[type]++;

            // liveCount[type] is how many of this type the entity actually
            // has. operator[] inserts a 0 for a type it never saw, which is
            // exactly the right answer.
            if (slot >= liveCount[type]) {
                instance.removedComponents.push_back(
                    PrefabRemovedComponent{localId, type, slot});
            }
        }
    });
}

std::size_t CapturePrefabOverridesForAsset(Scene& scene, std::string_view virtualPath) {
    std::string        error;
    const PrefabAsset* asset = PrefabLibrary::Load(virtualPath, error);
    if (asset == nullptr) {
        ENGINE_LOG_WARN(Channels::kScene,
                        "could not read '{}', so the instances of it keep the "
                        "overrides they already had: {}", virtualPath, error);
        return 0;
    }

    const std::string path(virtualPath);
    std::size_t       captured = 0;
    scene.ForEachPrefabInstance([&](PrefabInstance& instance) {
        if (instance.prefabPath != path) {
            return;
        }
        CapturePrefabOverrides(scene, *asset, instance, nullptr);
        ++captured;
    });
    return captured;
}

bool BuildPrefabDocumentFromEntity(Scene& scene, EntityId rootId, Json& outDocument,
                                   std::string& outError) {
    Entity* root = scene.Get(rootId);
    if (root == nullptr) {
        outError = "there is nothing there to make a prefab from";
        return false;
    }

    const std::unordered_map<const Transform2D*, Entity*> owners = OwnersByTransform(scene);

    // BREADTH FIRST, so a parent always has its id before any of its children
    // need it - and so the numbers in the file read in the same order as the
    // Hierarchy shows them, which matters when somebody opens the file.
    struct Pending {
        Entity* entity;
        int     parentId;
    };
    std::vector<Pending> queue{Pending{root, 0}};

    Json        entities = Json::array();
    int         nextId   = 1;
    std::size_t skipped  = 0;

    for (std::size_t i = 0; i < queue.size(); ++i) {
        Entity*   entity  = queue[i].entity;
        const int localId = nextId++;

        Json node    = Json::object();
        node["id"]   = localId;
        if (queue[i].parentId != 0) {
            node["parent"] = queue[i].parentId;
        }
        node["name"] = entity->Name();

        Json components = Json::array();
        entity->ForEachComponent([&](Component& component) {
            Json blob;
            if (!SerializeComponent(component, blob)) {
                ENGINE_LOG_WARN(Channels::kScene,
                                "making a prefab of '{}': '{}' cannot be saved and was "
                                "left out", entity->Name(), component.TypeName());
                ++skipped;
                return;
            }
            components.push_back(std::move(blob));
        });
        node["components"] = std::move(components);
        entities.push_back(std::move(node));

        for (Transform2D* child : entity->Transform().Children()) {
            if (const auto it = owners.find(child); it != owners.end()) {
                queue.push_back(Pending{it->second, localId});
            }
        }
    }

    outDocument = Json::object();

    // A FORMAT VERSION, written even though nothing reads it yet. The day
    // nested prefabs arrive, every file written before them needs to be
    // tellable apart from the ones written after - and a version number added
    // at that point is a version number that is absent from every existing
    // file, which is the one case it cannot help with.
    outDocument["prefab"]   = 1;
    outDocument["name"]     = root->Name();
    outDocument["root"]     = 1;
    outDocument["entities"] = std::move(entities);

    if (skipped > 0) {
        outError = "this entity has " + std::to_string(skipped) +
                   " component(s) that cannot be saved, so the prefab would not be a "
                   "complete copy of it";
        return false;
    }

    outError.clear();
    return true;
}

bool BuildAssetDocumentFromInstance(Scene& scene, const PrefabAsset& asset,
                                    const PrefabInstance& instance, Json& outDocument,
                                    std::string& outError) {
    // Started from the asset's own document, so every key this build does not
    // understand survives - the same promise BuildSceneDocument makes about a
    // scene file, and for the same reason.
    outDocument = asset.Document();
    if (!outDocument.is_object()) {
        outDocument = Json::object();
    }

    std::unordered_map<int, Entity*> members;
    scene.ForEachPrefabMember(instance, [&members](Entity& entity, int localId) {
        members[localId] = &entity;
    });

    Json        entities = Json::array();
    std::size_t skipped  = 0;

    for (const PrefabEntityNode& entry : asset.Entities()) {
        Json node  = entry.node;
        node["id"] = entry.localId;
        if (entry.parent != 0) {
            node["parent"] = entry.parent;
        } else {
            node.erase("parent");
        }

        const auto it = members.find(entry.localId);
        if (it == members.end()) {
            // The asset has an entity this instance does not. Kept exactly as
            // it is: the instance has nothing to say about it, and dropping it
            // would delete part of the prefab from every other scene.
            entities.push_back(std::move(node));
            continue;
        }

        // THE NAME STAYS THE ASSET'S. An instance's names are per-instance -
        // see PrefabInstance::names - and pushing "Muzzle_2" into the asset
        // would rename that child in every scene in the project.

        Json components = Json::array();
        it->second->ForEachComponent([&](Component& component) {
            Json blob;
            if (!SerializeComponent(component, blob)) {
                ++skipped;
                return;
            }
            components.push_back(std::move(blob));
        });

        // ---- the root's placement is left alone ---------------------------
        //
        // Where this instance STANDS is not something to push into the asset;
        // see IsRootPlacement. Without this, applying any change to an enemy
        // would move every other enemy in the project to wherever this one
        // happened to be.
        if (entry.localId == asset.RootId()) {
            const Json& assetComponents = Field(entry.node, "components");
            const int   assetTransform =
                ComponentIndex(assetComponents, TransformComponent::kTypeName, 0);
            const int liveTransform =
                ComponentIndex(components, TransformComponent::kTypeName, 0);
            if (assetTransform >= 0 && liveTransform >= 0) {
                const Json& from =
                    assetComponents[static_cast<std::size_t>(assetTransform)];
                Json& into = components[static_cast<std::size_t>(liveTransform)];
                for (const char* key : {"position", "rotation"}) {
                    if (HasKey(from, key)) {
                        into[key] = from.at(key);
                    } else {
                        into.erase(key);
                    }
                }
            }
        }

        node["components"] = std::move(components);
        entities.push_back(std::move(node));
    }

    outDocument["prefab"]   = 1;
    outDocument["entities"] = std::move(entities);
    if (!HasKey(outDocument, "root")) {
        outDocument["root"] = asset.RootId();
    }
    if (!HasKey(outDocument, "name")) {
        outDocument["name"] = asset.Name();
    }

    if (skipped > 0) {
        outError = "this instance has " + std::to_string(skipped) +
                   " component(s) that cannot be saved, so applying it would leave "
                   "them out of the prefab";
        return false;
    }

    outError.clear();
    return true;
}

// ---------------------------------------------------------------------------
//  WHAT COUNTS AS AN OVERRIDE
// ---------------------------------------------------------------------------

bool IsRootPlacement(const PrefabInstance& instance, const PrefabOverride& item) {
    // instance.rootLocalId rather than asset.RootId(), because applying drops
    // the asset from the cache before this is reached. See the header.
    if (item.localId == 0 || item.localId != instance.rootLocalId) {
        return false;
    }
    if (item.component != TransformComponent::kTypeName) {
        return false;
    }
    return item.field == "position" || item.field == "rotation";
}

std::string DescribeOverride(const PrefabAsset& asset, const PrefabOverride& item) {
    return AssetEntityName(asset, item.localId) + " / " +
           DescribeComponent(item.component, item.slot) + " / " + item.field;
}

std::string DescribeAddedComponent(const PrefabAsset& asset,
                                   const PrefabAddedComponent& item) {
    return AssetEntityName(asset, item.localId) + " / added " + TypeOf(item.component);
}

std::string DescribeRemovedComponent(const PrefabAsset& asset,
                                     const PrefabRemovedComponent& item) {
    return AssetEntityName(asset, item.localId) + " / removed " +
           DescribeComponent(item.component, item.slot);
}

std::vector<PrefabOverrideItem> ListPrefabOverrides(Scene& scene, const PrefabAsset& asset,
                                                    PrefabInstance& instance) {
    // DIFFED FIRST, so the list always describes the scene as it is now rather
    // than as it was when it was last saved. This is what the Inspector calls
    // every frame it draws an instance.
    CapturePrefabOverrides(scene, asset, instance, nullptr);

    std::vector<PrefabOverrideItem> out;
    out.reserve(instance.OverrideCount());

    for (const PrefabOverride& item : instance.overrides) {
        if (IsRootPlacement(instance, item)) {
            continue;
        }
        PrefabOverrideItem entry;
        entry.kind        = PrefabOverrideKind::Property;
        entry.property    = item;
        entry.description = DescribeOverride(asset, item);
        out.push_back(std::move(entry));
    }
    for (const PrefabAddedComponent& item : instance.addedComponents) {
        PrefabOverrideItem entry;
        entry.kind        = PrefabOverrideKind::AddedComponent;
        entry.added       = item;
        entry.description = DescribeAddedComponent(asset, item);
        out.push_back(std::move(entry));
    }
    for (const PrefabRemovedComponent& item : instance.removedComponents) {
        PrefabOverrideItem entry;
        entry.kind        = PrefabOverrideKind::RemovedComponent;
        entry.removed     = item;
        entry.description = DescribeRemovedComponent(asset, item);
        out.push_back(std::move(entry));
    }

    std::sort(out.begin(), out.end(),
              [](const PrefabOverrideItem& a, const PrefabOverrideItem& b) {
                  return a.description < b.description;
              });
    return out;
}

// ---------------------------------------------------------------------------
//  APPLY AND REVERT
// ---------------------------------------------------------------------------

namespace {

struct Resolved {
    PrefabInstance*    instance = nullptr;
    const PrefabAsset* asset    = nullptr;
};

bool Resolve(Scene& scene, EntityId member, Resolved& out, std::string& outError) {
    out.instance = scene.PrefabInstanceOf(member);
    if (out.instance == nullptr) {
        outError = "that entity is not part of a prefab instance";
        return false;
    }
    std::string loadError;
    out.asset = PrefabLibrary::Load(out.instance->prefabPath, loadError);
    if (out.asset == nullptr) {
        outError = "could not read the prefab this is an instance of - " + loadError;
        return false;
    }
    return true;
}

// Writes a prefab document to its file. The one place that happens, so the
// indentation and the cache invalidation cannot be done two different ways.
bool WriteAsset(const std::string& path, const Json& document, std::string& outError) {
    // dump(2) for the same reason a scene is saved that way: a person opens
    // these files and a version-control diff has to make sense.
    if (!FileSystem::WriteTextFile(path, document.dump(2), outError)) {
        return false;
    }
    PrefabLibrary::Forget(path);
    return true;
}

// The entity node for one local id inside a prefab DOCUMENT - the mutable
// counterpart of PrefabAsset::Find, used by the single-item applies.
Json* FindEntityNode(Json& document, int localId) {
    const auto entities = document.find("entities");
    if (entities == document.end() || !entities->is_array()) {
        return nullptr;
    }
    for (Json& node : *entities) {
        if (node.is_object() && ReadInt(node, "id", 0) == localId) {
            return &node;
        }
    }
    return nullptr;
}

// Drops the override records this item stands for, so that the instance stops
// claiming a difference it has just pushed into the asset.
//
// WITHOUT THIS the record would hold an override whose value matches the asset
// exactly. Harmless in effect - the next diff drops it - but it would be
// written to the scene file in between, and a scene file claiming overrides
// that are not overrides is a scene file somebody will eventually try to
// understand.
void ForgetItem(PrefabInstance& instance, const PrefabOverrideItem& item) {
    switch (item.kind) {
        case PrefabOverrideKind::Property:
            std::erase_if(instance.overrides, [&item](const PrefabOverride& held) {
                return held.localId == item.property.localId &&
                       held.component == item.property.component &&
                       held.slot == item.property.slot &&
                       held.field == item.property.field;
            });
            break;
        case PrefabOverrideKind::AddedComponent:
            std::erase_if(instance.addedComponents,
                          [&item](const PrefabAddedComponent& held) {
                              return held.localId == item.added.localId &&
                                     held.component == item.added.component;
                          });
            break;
        case PrefabOverrideKind::RemovedComponent:
            std::erase_if(instance.removedComponents,
                          [&item](const PrefabRemovedComponent& held) {
                              return held.localId == item.removed.localId &&
                                     held.component == item.removed.component &&
                                     held.slot == item.removed.slot;
                          });
            break;
    }
}

} // namespace

bool ApplyPrefabInstance(Scene& scene, EntityId member, std::string& outError) {
    Resolved target;
    if (!Resolve(scene, member, target, outError)) {
        return false;
    }
    const std::string path = target.instance->prefabPath;

    // ======================================================================
    //  THE ORDER OF THESE FIVE STEPS IS THE WHOLE FUNCTION.
    //
    //  1. Every instance of this asset - INCLUDING the other ones - has its
    //     overrides worked out against the asset AS IT IS NOW. This has to
    //     happen before the file changes, because a diff against the NEW
    //     asset would record each other instance's current values as
    //     deliberate overrides and pin it there forever. That is the exact
    //     opposite of what applying is for.
    // ======================================================================
    CapturePrefabOverridesForAsset(scene, path);

    // 2. The new asset, built from this instance's live entities.
    Json document;
    if (!BuildAssetDocumentFromInstance(scene, *target.asset, *target.instance, document,
                                        outError)) {
        return false;
    }

    // 3. Written to disk. Nothing in the scene has changed yet, so a failure
    //    here leaves everything exactly as it was.
    if (!WriteAsset(path, document, outError)) {
        return false;
    }

    // ======================================================================
    //  4. This instance has no overrides left - they are in the asset now -
    //     except its PLACEMENT, which was never something to apply.
    //
    //  NOTE WHAT IS NOT TOUCHED HERE: target.asset. Step 3 wrote the file,
    //  and writing drops the asset from PrefabLibrary, so that pointer is
    //  dangling from this line onwards. Everything below works from the
    //  instance record, which is not going anywhere.
    // ======================================================================
    std::vector<PrefabOverride> placement;
    for (const PrefabOverride& item : target.instance->overrides) {
        if (IsRootPlacement(*target.instance, item)) {
            placement.push_back(item);
        }
    }
    target.instance->overrides = std::move(placement);
    target.instance->addedComponents.clear();
    target.instance->removedComponents.clear();

    // 5. Re-read the asset and rebuild every instance of it, each keeping
    //    whatever step 1 recorded for it.
    std::string rebuildError;
    const std::size_t rebuilt = scene.RebuildPrefabInstances(path, rebuildError);
    if (rebuilt == 0 && !rebuildError.empty()) {
        outError = rebuildError;
        return false;
    }

    ENGINE_LOG_INFO(Channels::kScene,
                    "applied to '{}'; {} instance(s) in this scene rebuilt", path,
                    rebuilt);
    outError.clear();
    return true;
}

bool RevertPrefabInstance(Scene& scene, EntityId member, std::string& outError) {
    Resolved target;
    if (!Resolve(scene, member, target, outError)) {
        return false;
    }

    // ======================================================================
    //  DIFFED FIRST, EVEN THOUGH EVERYTHING IS ABOUT TO BE THROWN AWAY.
    //
    //  The record's override vectors are DERIVED - they hold nothing until
    //  something captures them, which normally happens when the scene is
    //  saved. So reverting an instance that had not been saved since it was
    //  moved would find an empty list, keep no placement out of it, and
    //  rebuild the instance at the asset's origin: Revert would TELEPORT the
    //  thing you were working on.
    // ======================================================================
    CapturePrefabOverrides(scene, *target.asset, *target.instance, nullptr);

    // The placement survives, so reverting does not teleport the instance to
    // the asset's origin. See IsRootPlacement.
    std::vector<PrefabOverride> placement;
    for (const PrefabOverride& item : target.instance->overrides) {
        if (IsRootPlacement(*target.instance, item)) {
            placement.push_back(item);
        }
    }
    target.instance->overrides = std::move(placement);
    target.instance->addedComponents.clear();
    target.instance->removedComponents.clear();

    return scene.RebuildPrefabInstance(*target.instance, outError);
}

bool ApplyPrefabItem(Scene& scene, EntityId member, const PrefabOverrideItem& item,
                     std::string& outError) {
    Resolved target;
    if (!Resolve(scene, member, target, outError)) {
        return false;
    }
    const std::string path = target.instance->prefabPath;

    // Same first step as applying everything, and for the same reason.
    CapturePrefabOverridesForAsset(scene, path);

    Json document = target.asset->Document();
    if (!document.is_object()) {
        outError = "the prefab file is not a JSON object";
        return false;
    }

    const int localId = item.kind == PrefabOverrideKind::Property ? item.property.localId
                        : item.kind == PrefabOverrideKind::AddedComponent
                            ? item.added.localId
                            : item.removed.localId;

    Json* entityNode = FindEntityNode(document, localId);
    if (entityNode == nullptr) {
        outError = "the prefab no longer has the entity this change belongs to";
        return false;
    }
    Json& components = (*entityNode)["components"];
    if (!components.is_array()) {
        components = Json::array();
    }

    switch (item.kind) {
        case PrefabOverrideKind::Property: {
            const int index =
                ComponentIndex(components, item.property.component, item.property.slot);
            if (index < 0) {
                outError = "the prefab no longer has the component this change belongs to";
                return false;
            }
            if (item.property.value.is_null()) {
                EraseField(components[static_cast<std::size_t>(index)],
                           item.property.field);
            } else {
                SetField(components[static_cast<std::size_t>(index)], item.property.field,
                         item.property.value);
            }
            break;
        }
        case PrefabOverrideKind::AddedComponent:
            components.push_back(item.added.component);
            break;
        case PrefabOverrideKind::RemovedComponent: {
            const int index =
                ComponentIndex(components, item.removed.component, item.removed.slot);
            if (index < 0) {
                // Already gone from the asset, which is the state being asked
                // for. Reported as success rather than as an error, because
                // the person's intent has been achieved.
                break;
            }
            components.erase(components.begin() + index);
            break;
        }
    }

    if (!WriteAsset(path, document, outError)) {
        return false;
    }

    ForgetItem(*target.instance, item);

    std::string       rebuildError;
    const std::size_t rebuilt = scene.RebuildPrefabInstances(path, rebuildError);
    if (rebuilt == 0 && !rebuildError.empty()) {
        outError = rebuildError;
        return false;
    }

    ENGINE_LOG_INFO(Channels::kScene, "applied {} to '{}'; {} instance(s) rebuilt",
                    item.description, path, rebuilt);
    outError.clear();
    return true;
}

bool RevertPrefabItem(Scene& scene, EntityId member, const PrefabOverrideItem& item,
                      std::string& outError) {
    Resolved target;
    if (!Resolve(scene, member, target, outError)) {
        return false;
    }

    // DIFFED FIRST. The item was described while the Inspector was drawing and
    // is being acted on now, and in between the vectors on the record may hold
    // nothing at all - they are rebuilt by every capture. Diffing here means
    // the entry this is about to remove is certainly in them.
    CapturePrefabOverrides(scene, *target.asset, *target.instance, nullptr);
    ForgetItem(*target.instance, item);

    return scene.RebuildPrefabInstance(*target.instance, outError);
}

// ---------------------------------------------------------------------------
//  MAKING ONE
// ---------------------------------------------------------------------------

bool CreatePrefabFromEntity(Scene& scene, EntityId rootId, std::string_view virtualPath,
                            EntityId& outRoot, std::string& outError) {
    outRoot = EntityId{};

    Entity* root = scene.Get(rootId);
    if (root == nullptr) {
        outError = "there is nothing selected to make a prefab from";
        return false;
    }
    if (!FileSystem::HasExtension(virtualPath, kPrefabExtension)) {
        outError = std::string("a prefab has to be saved with a ") + kPrefabExtension +
                   " name";
        return false;
    }
    if (FileSystem::Exists(virtualPath)) {
        // REFUSED RATHER THAN MERGED. Writing over an existing prefab would
        // reach every instance of it in every scene in the project, which is
        // not something to do as a side effect of dragging something into a
        // folder. Applying to an existing prefab is a separate, named act.
        outError = "'" + std::string(virtualPath) +
                   "' already exists - to change that prefab, edit one of its "
                   "instances and press Apply";
        return false;
    }

    // ======================================================================
    //  NOT OUT OF ANYTHING THAT IS ALREADY PART OF A PREFAB, at any depth.
    //
    //  Nested prefabs are not supported - see the list at the top of Prefab.h
    //  - so saving a subtree with an instance in it would FLATTEN that
    //  instance: the new file would hold plain copies of its entities, with no
    //  link back to the prefab they came from. The copies would then sit there
    //  looking like prefab content and quietly ignoring every later change to
    //  the prefab they were taken from.
    //
    //  Refused and named, rather than done silently. The editor checks the
    //  dragged entity itself before it gets this far, for a quicker message;
    //  this is the check that also covers a child three levels down, and the
    //  one that holds for any caller.
    // ======================================================================
    {
        const std::unordered_map<const Transform2D*, Entity*> owners =
            OwnersByTransform(scene);
        std::vector<Entity*> queue{root};
        for (std::size_t i = 0; i < queue.size(); ++i) {
            if (scene.PrefabInstanceOf(queue[i]->Id()) != nullptr) {
                outError = "'" + queue[i]->Name() +
                           "' is part of a prefab instance, so this cannot be saved as "
                           "a prefab of its own - a prefab inside a prefab is not "
                           "supported yet. Unpack that instance first.";
                return false;
            }
            for (Transform2D* child : queue[i]->Transform().Children()) {
                if (const auto it = owners.find(child); it != owners.end()) {
                    queue.push_back(it->second);
                }
            }
        }
    }

    // Remembered BEFORE anything is destroyed. The entity about to become an
    // instance is about to stop existing.
    const std::string name = root->Name();
    std::string       parentName;
    if (const Transform2D* parent = root->Transform().Parent(); parent != nullptr) {
        const std::unordered_map<const Transform2D*, Entity*> owners =
            OwnersByTransform(scene);
        if (const auto it = owners.find(parent); it != owners.end()) {
            parentName = it->second->Name();
        }
    }

    Json document;
    if (!BuildPrefabDocumentFromEntity(scene, rootId, document, outError)) {
        return false;
    }
    if (!WriteAsset(std::string(virtualPath), document, outError)) {
        return false;
    }

    // ======================================================================
    //  THE ORIGINAL IS REPLACED BY AN INSTANCE OF WHAT IT JUST BECAME.
    //
    //  This is what Unity does when you drag a GameObject into the Project
    //  window, and it is the difference between a prefab system and an export
    //  button: what you were working on is now linked to the asset, so the
    //  next change you make to it can be applied to every copy.
    //
    //  The entities go first, so the names they were using are free for the
    //  instance - which is what lets the instance come back with exactly the
    //  same names rather than "Enemy_1".
    //
    //  THE WHOLE SUBTREE, NOT JUST THE ROOT. DestroyEntityImmediate hands an
    //  entity's children back to the WORLD, so destroying only the root left
    //  every descendant behind - and since those descendants still held the
    //  names the asset uses, the instance came back as "Barrel_1" beside an
    //  orphaned "Barrel". Collected before anything is destroyed, because
    //  destroying rearranges the tree being walked.
    // ======================================================================
    std::vector<EntityId> doomed;
    {
        const std::unordered_map<const Transform2D*, Entity*> owners =
            OwnersByTransform(scene);
        std::vector<Entity*> queue{root};
        for (std::size_t i = 0; i < queue.size(); ++i) {
            doomed.push_back(queue[i]->Id());
            for (Transform2D* child : queue[i]->Transform().Children()) {
                if (const auto it = owners.find(child); it != owners.end()) {
                    queue.push_back(it->second);
                }
            }
        }
    }
    for (const EntityId id : doomed) {
        scene.DestroyEntityImmediate(id);
    }

    const EntityId instanceRoot = scene.InstantiatePrefab(virtualPath, outError);
    if (instanceRoot.IsNull()) {
        return false;
    }

    // The asset's root is called `name` already, because that is what it was
    // written from - but a sibling could have taken the name in between, and
    // a rename that is not needed costs nothing.
    if (Entity* created = scene.Get(instanceRoot); created != nullptr &&
                                                   created->Name() != name) {
        scene.RenameEntity(instanceRoot, name);
    }

    if (!parentName.empty()) {
        if (PrefabInstance* instance = scene.PrefabInstanceOf(instanceRoot)) {
            instance->parentName = parentName;
        }
        Entity* parent   = scene.Get(scene.Find(parentName));
        Entity* instance = scene.Get(instanceRoot);
        if (parent != nullptr && instance != nullptr) {
            // keepWorldTransform stays false: the local numbers came out of
            // the asset, which was written from this entity while it had that
            // same parent, so keeping them puts it back exactly where it was.
            instance->Transform().SetParent(&parent->Transform());
        }
    }

    outRoot = instanceRoot;
    outError.clear();
    return true;
}

} // namespace eng
