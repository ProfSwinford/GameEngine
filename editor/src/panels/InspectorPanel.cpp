// ============================================================================
//  InspectorPanel.cpp - the Inspector. See InspectorPanel.h for the three
//  answers about editing while the game is running.
//
//  Each component type gets its own small draw function below. Adding a new
//  one means writing another and adding a line to the chain in
//  InspectorPanel::Draw - editor work, which touches no engine code at all.
// ============================================================================

#include "panels/InspectorPanel.h"

#include "AssetDragDrop.h"
#include "EditorApp.h"
#include "panels/HierarchyPanel.h"   // kPayloadEntity, for dropping an entity on a reference
#include "ScriptBuild.h"              // IsBuilding, for the NOT FOUND explanation
#include "ShellOpen.h"

#include <engine/render/Renderer.h>
#include <engine/fs/FileSystem.h>   // ListDirectory, for the asset pickers
#include <engine/resource/ImportSettings.h>
#include <engine/resource/ResourceManager.h>
#include <engine/scene/Prefab.h>

#include <imgui.h>
#include <imgui_internal.h>   // GetCurrentWindow, for a whole-window drop target

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace editor {
namespace {

// ===========================================================================
//  A SLOT HOLDING A REFERENCE TO AN ASSET FILE
//
//  Three components point at a picture - Sprite, Sprite Animation and UI Image
//  - and each of them used to show a full virtual path in a text box. That was
//  one mistake made twice.
//
//  THE PATH DOES NOT FIT. "textures/Sprites/Blue/srw_arpg_spear01_1.png#f0" in
//  a box the width of the Inspector shows you the middle of a path with both
//  ends cut off, and the end is the part that identifies it. The NAME is what
//  anybody recognises; the folders above it are something you already know,
//  because you put it there.
//
//  AND A TEXT BOX IS NOT A DROP TARGET YOU CAN SEE. The whole Inspector does
//  accept an image - see the window-wide target at the bottom of this file -
//  but "drop it anywhere on the panel" is not what anybody tries, and what it
//  does is give the ENTITY an asset, which may mean adding a component rather
//  than replacing what the field under the cursor is holding.
//
//  So a slot is a BUTTON, exactly as the entity and prefab references already
//  are: the current value's name, a rectangle big enough to drop onto, and a
//  picker behind the click. The full path lives in the tooltip, because the
//  one moment somebody wants it is the moment something is wrong.
// ===========================================================================
enum class AssetSlotKind {
    Image,       // any picture, including one frame cut out of a sliced sheet
    Animation,   // a named animation stored in a sheet, and nothing else
};

// What to put on the button: the asset's display name, or the word for having
// nothing. eng::AssetDisplayName does the naming and is tested; the "(none)"
// is a thing about widgets and stays here.
std::string AssetSlotName(std::string_view virtualPath) {
    if (virtualPath.empty()) {
        return "(none)";
    }
    return eng::AssetDisplayName(virtualPath);
}

// The folder a reference lives in, for the picker's second column. Empty at
// the top of assets/.
std::string AssetSlotFolder(std::string_view virtualPath) {
    const std::string file  = eng::SheetPathOf(virtualPath);
    const std::size_t slash = file.find_last_of('/');
    return (slash == std::string::npos) ? std::string{} : file.substr(0, slash);
}

bool ContainsNoCase(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    const auto at = std::search(
        haystack.begin(), haystack.end(), needle.begin(), needle.end(),
        [](unsigned char a, unsigned char b) {
            return std::tolower(a) == std::tolower(b);
        });
    return at != haystack.end();
}

// Everything that could go in a slot of this kind. The same recursive shape
// CollectTiles uses in the Tile Palette, and walked only while a picker is
// open - never per frame.
void CollectSlotCandidates(const std::string& directory, AssetSlotKind kind,
                           std::vector<std::string>& out) {
    std::vector<eng::FileSystem::DirEntry> entries;
    if (!eng::FileSystem::ListDirectory(directory, entries)) {
        return;
    }

    for (const eng::FileSystem::DirEntry& entry : entries) {
        if (entry.isDirectory) {
            CollectSlotCandidates(entry.virtualPath, kind, out);
            continue;
        }
        if (ClassifyAsset(entry.virtualPath) != AssetKind::Texture) {
            continue;
        }

        // THE WHOLE IMAGE COUNTS FOR A SPRITE AND NOT FOR AN ANIMATION,
        // because an animation is a thing stored INSIDE a sheet - a path names
        // a picture, and an animation is not one. See SpriteAnimation.h.
        if (kind == AssetSlotKind::Image) {
            out.push_back(entry.virtualPath);
        }

        const eng::TextureImport imported =
            eng::ImportLibrary::ForTexture(entry.virtualPath);

        if (kind == AssetSlotKind::Image) {
            for (const eng::SheetFrame& frame : imported.sheet.frames) {
                out.push_back(eng::MakeSubAssetPath(entry.virtualPath, frame.name));
            }
        } else {
            for (const eng::SheetAnimation& animation : imported.sheet.animations) {
                out.push_back(eng::MakeSubAssetPath(entry.virtualPath, animation.name));
            }
        }
    }
}

// Draws the slot. True on a frame something new was chosen, and `outPath` is
// then what it should become - empty meaning cleared.
//
// `broken` paints the name red: the reference is set and does not resolve,
// which is the one state a name alone could not tell you about.
bool DrawAssetSlot(const char* label, AssetSlotKind kind, const std::string& current,
                   bool broken, const char* emptyHint, std::string& outPath) {
    bool changed = false;
    ImGui::PushID(label);

    const std::string shown = AssetSlotName(current);

    if (broken) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.40f, 1.0f));
    } else if (current.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    }

    // Sized like the drag boxes above it, so the column of fields lines up -
    // and so there is a rectangle to drop onto rather than a word.
    const bool pick =
        ImGui::Button(shown.c_str(), ImVec2(ImGui::CalcItemWidth(), 0.0f));

    if (broken || current.empty()) {
        ImGui::PopStyleColor();
    }

    // AGAINST THE BUTTON, while the button is still the last item drawn.
    if (ImGui::IsItemHovered()) {
        if (current.empty()) {
            ImGui::SetTooltip("%s", emptyHint);
        } else {
            ImGui::SetTooltip("%s\n\nDrop an image here to replace it, or click to "
                              "choose one.",
                              current.c_str());
        }
    }

    // ---- the drop, which is the gesture this exists for -------------------
    //
    // A FIELD TARGET BEATS THE WINDOW-WIDE ONE, and that is ImGui's own rule
    // rather than luck: the smallest accepting rectangle under the cursor gets
    // the payload. So dropping on the slot replaces what the slot holds, and
    // dropping anywhere else on the panel still means "give this entity the
    // asset" - which may add a component. Two gestures, two meanings, and the
    // one under your cursor is the one you get.
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload(kPayloadTexture)) {
            // Every asset payload in this editor is the virtual path as text
            // with its terminator - see AssetDragDrop.h.
            outPath.assign(static_cast<const char*>(payload->Data));
            changed = true;
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::SameLine();
    ImGui::TextUnformatted(label);

    // ---- the picker, for when both ends are not on screen at once ---------
    //
    // The same reason the entity reference has one: dragging across a docked
    // layout you cannot see both ends of is worse than a list. It also stands
    // in for the text box this slot replaced - there is no path to type any
    // more, so there has to be a way to choose one without the Assets panel
    // in view.
    static char filter[64] = {};
    if (pick) {
        filter[0] = '\0';
        ImGui::OpenPopup("pick-asset");
    }
    if (ImGui::BeginPopup("pick-asset")) {
        ImGui::TextDisabled(kind == AssetSlotKind::Animation
                                ? "animations stored in this project's sheets"
                                : "images, and the frames of any sliced sheet");

        ImGui::SetNextItemWidth(300.0f);
        ImGui::InputTextWithHint("##filter", "type to narrow this down", filter,
                                 sizeof(filter));
        ImGui::Separator();

        if (ImGui::Selectable("(none)", current.empty())) {
            outPath.clear();
            changed = true;
        }

        // WALKED HERE, inside the popup, so a project with two thousand images
        // costs nothing on the frames when nobody is choosing one.
        std::vector<std::string> candidates;
        CollectSlotCandidates("", kind, candidates);

        ImGui::BeginChild("list", ImVec2(360.0f, 320.0f));
        int offered = 0;
        for (const std::string& candidate : candidates) {
            const std::string name = AssetSlotName(candidate);
            if (!ContainsNoCase(candidate, filter)) {
                continue;
            }
            ++offered;

            if (ImGui::Selectable(name.c_str(), candidate == current)) {
                outPath = candidate;
                changed = true;
            }

            // THE FOLDER BESIDE THE NAME, because two files can share a name
            // and the whole point of this slot is that the name is all you
            // normally need. When it is not enough, it is right here.
            if (const std::string folder = AssetSlotFolder(candidate);
                !folder.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", folder.c_str());
            }
        }
        if (offered == 0) {
            if (filter[0] != '\0') {
                ImGui::TextDisabled("nothing matches '%s'", filter);
            } else if (kind == AssetSlotKind::Animation) {
                // NAMING WHAT IS MISSING, because an empty list reads as a
                // broken picker rather than as an answer.
                ImGui::TextDisabled("no sheet in this project has an animation in");
                ImGui::TextDisabled("it yet - slice one in the Inspector with the");
                ImGui::TextDisabled("image selected, then add animations to it");
            } else {
                ImGui::TextDisabled("no images under assets/ yet");
            }
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }

    ImGui::PopID();
    return changed;
}

// ---------------------------------------------------------------------------
//  TransformComponent
// ---------------------------------------------------------------------------
void DrawTransform(eng::TransformComponent& component, bool running) {
    eng::Transform2D& transform = component.Transform();

    // Answer 2 from the header: while the game is running, a system may own
    // these values. They are shown but not editable, and the reason is written
    // on screen rather than left as a mystery.
    ImGui::BeginDisabled(running);

    // ImGui's DragFloat widgets want a plain array of floats, so the Vec2 is
    // unpacked into one, edited, and packed back if it changed. Each of these
    // returns true only on a frame the value was actually changed.
    float position[2] = {transform.LocalPosition().x, transform.LocalPosition().y};
    if (ImGui::DragFloat2("Position", position, 0.5f)) {
        transform.SetLocalPosition(eng::Vec2{position[0], position[1]});
        EditorState::Get().MarkDirty();
    }

    // Shown in DEGREES, stored in radians. Every angle a person sees in this
    // editor is degrees; every angle the engine holds is radians. The
    // conversion happens here, at the edge, exactly once.
    float degrees = transform.LocalRotation() * eng::kRadToDeg;
    if (ImGui::DragFloat("Rotation", &degrees, 0.5f, -360.0f, 360.0f, "%.1f deg")) {
        transform.SetLocalRotation(degrees * eng::kDegToRad);
        EditorState::Get().MarkDirty();
    }

    float scale[2] = {transform.LocalScale().x, transform.LocalScale().y};
    if (ImGui::DragFloat2("Scale", scale, 0.01f)) {
        transform.SetLocalScale(eng::Vec2{scale[0], scale[1]});
        EditorState::Get().MarkDirty();
    }

    ImGui::EndDisabled();
    if (running) {
        ImGui::TextDisabled("read-only while playing - a system may own these. "
                            "Pause to edit.");
    }

    // The LOCAL values are above; the WORLD position is shown as well, because
    // for a child entity those two are different and the difference is exactly
    // what confuses people about parenting.
    const eng::Vec2 world = transform.WorldPosition();
    ImGui::TextDisabled("world position: %.2f, %.2f   (%d parent(s) above it)",
                        static_cast<double>(world.x), static_cast<double>(world.y),
                        transform.Depth());
}

// ---------------------------------------------------------------------------
//  SpriteComponent
// ---------------------------------------------------------------------------
void DrawSprite(eng::SpriteComponent& sprite) {
    // SET AND NOT RESOLVING is the one thing a name cannot show you, so it is
    // what paints the slot red. A missing image comes back as the magenta
    // placeholder rather than as nothing, which is why this asks the texture
    // and not just the path.
    const eng::TextureRef& loaded = sprite.GetTexture();
    const bool             broken = !sprite.TexturePath().empty() &&
                        (!loaded || loaded->isPlaceholder);

    std::string chosen;
    if (DrawAssetSlot("Texture", AssetSlotKind::Image, sprite.TexturePath(), broken,
                      "drag an image here from Assets, or click to choose one",
                      chosen)) {
        sprite.SetTexture(chosen);
        EditorState::Get().MarkDirty();
    }
    if (sprite.TexturePath().empty()) {
        ImGui::TextDisabled("drag an image here from Assets, or click to choose one");
    }

    // ImGui's colour picker works in 0-1 floats; the engine stores 0-255
    // bytes, because that is what an image editor shows you. Converted here.
    const eng::Color tint = sprite.Tint();
    float rgba[4] = {tint.r / 255.0f, tint.g / 255.0f, tint.b / 255.0f, tint.a / 255.0f};
    if (ImGui::ColorEdit4("Tint", rgba)) {
        sprite.SetTint(eng::Color{static_cast<unsigned char>(rgba[0] * 255.0f),
                                  static_cast<unsigned char>(rgba[1] * 255.0f),
                                  static_cast<unsigned char>(rgba[2] * 255.0f),
                                  static_cast<unsigned char>(rgba[3] * 255.0f)});
        EditorState::Get().MarkDirty();
    }

    int layer = sprite.Layer();
    if (ImGui::DragInt("Draw order", &layer, 0.2f)) {
        sprite.SetLayer(layer);
        EditorState::Get().MarkDirty();
    }
    ImGui::TextDisabled("higher numbers are drawn on top");

    // A preview of the actual image, plus how many things are sharing it.
    if (const eng::TextureRef& texture = sprite.GetTexture(); texture) {
        ImGui::Image(reinterpret_cast<ImTextureID>(texture->native), ImVec2(64, 64));
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::Text("%d x %d pixels", texture->width, texture->height);

        // use_count() is how many shared_ptrs are keeping this image loaded -
        // one per sprite using it, plus this preview. It is the clearest
        // possible demonstration of what shared ownership means.
        ImGui::TextDisabled("shared by %ld thing(s)",
                            static_cast<long>(texture.use_count()));
        if (texture->isPlaceholder) {
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f),
                               "this image failed to load");
        }
        ImGui::EndGroup();
    }
}

// ---------------------------------------------------------------------------
//  SpriteAnimationComponent
//
//  THE SECTION IS LIVE AGAINST THE SLICER even though nothing is playing.
//  Everything below reads ResolvedFrames(), which is the same function the
//  system calls - so the frame count, the rate and the diagnosis are whatever
//  is true of the sheet right now, and editing the animation in the slicer
//  above is reflected here on the next frame rather than on the next Play.
//
//  IT SAYS "PRESS PLAY" RATHER THAN SHOWING A FRAME NUMBER. An animation does
//  not run in the editor - a paused clock asks for no simulation steps - and a
//  panel that showed "frame 0 of 12" while nothing moved would read as a
//  broken animation rather than as a stopped one.
// ---------------------------------------------------------------------------
void DrawSpriteAnimation(eng::SpriteAnimationComponent& animation) {
    // ---- which animation --------------------------------------------------
    //
    // THE SAME SLOT AS THE SPRITE'S, deliberately: these two rows sit on the
    // same entity, one above the other, and two different ways of naming a
    // picture would be two things to learn for no reason.
    //
    // A WHOLE IMAGE DROPPED HERE IS ACCEPTED AND THEN EXPLAINED, rather than
    // refused. SetAnimationPath says "that is a whole image, not a part of
    // one" and Problem() puts it on screen below - which is the same
    // information as a refusal, without leaving somebody holding the thing
    // they meant and no idea why nothing happened.
    {
        std::string chosen;
        if (DrawAssetSlot("Animation", AssetSlotKind::Animation,
                          animation.AnimationPath(),
                          !animation.Problem().empty(),
                          "drag an animation here from Assets - expand a sheet's "
                          "row to find them",
                          chosen)) {
            if (animation.SetAnimationPath(chosen)) {
                EditorState::Get().MarkDirty();
            }
        }
    }

    // ---- what that path actually comes to ---------------------------------
    //
    // ASKED FIRST, because everything read below it is a plain read of
    // whatever this last found. See the note on ResolvedFrames.
    const std::size_t resolved = animation.ResolvedFrames().size();
    const std::size_t listed   = animation.FramesListed();

    if (!animation.Problem().empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s",
                           animation.Problem().c_str());
    } else if (animation.AnimationPath().empty()) {
        ImGui::TextDisabled("choose an animation stored in a spritesheet - slice one");
        ImGui::TextDisabled("in the Inspector with the image selected");
    } else {
        ImGui::Text("%zu frame(s) at %.1f fps, %s", resolved,
                    static_cast<double>(animation.SheetFps()),
                    animation.Loops() ? "repeating" : "played once");
        ImGui::TextDisabled("from %s", animation.SheetPath().c_str());

        // ONE NAME RENAMED AWAY IN THE SLICER LEAVES ITS OLD NAME HERE, and a
        // resolved animation is then quietly SHORTER than it reads. This is
        // the only place that holds both numbers, so it is the only place that
        // can say so.
        if (listed > resolved) {
            ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f),
                               "%zu of the %zu frames it lists are no longer in the",
                               listed - resolved, listed);
            ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f),
                               "sheet, so they are skipped");
        }
    }

    ImGui::Separator();

    // ---- the two things that ARE this entity's own ------------------------
    float speed = animation.Speed();
    if (ImGui::DragFloat("Speed", &speed, 0.01f, 0.05f, 20.0f, "%.2fx")) {
        animation.SetSpeed(speed);
        EditorState::Get().MarkDirty();
    }
    ImGui::TextDisabled("a multiplier on the sheet's rate, so 2 is twice as fast");

    bool playOnStart = animation.PlayOnStart();
    if (ImGui::Checkbox("Play when the scene starts", &playOnStart)) {
        animation.SetPlayOnStart(playOnStart);
        EditorState::Get().MarkDirty();
    }

    // ======================================================================
    //  THE FRAME RATE AND THE REPEAT FLAG ARE NOT HERE, and saying where they
    //  are is the point. They belong to the ANIMATION, in the sheet, so every
    //  entity playing it follows one edit - and the boxes for them are a few
    //  inches up this same panel when the sheet is selected.
    // ======================================================================
    ImGui::TextDisabled("the rate and whether it repeats belong to the sheet:");
    ImGui::TextDisabled("select the image and edit the animation there");

    ImGui::Separator();

    // ---- what it is doing right now ---------------------------------------
    if (!eng::Engine::Get().IsInPlayMode()) {
        // ANSWER 2 FROM InspectorPanel.h, in this component's own terms: an
        // animation is a system-owned value while the game runs, and in edit
        // mode there is no value to show because nothing has run.
        ImGui::TextDisabled("not playing - animations run in Play mode.");
        ImGui::TextDisabled("Pause and Step there to advance one frame at a time.");
        return;
    }

    if (animation.CurrentFrame() == eng::SpriteAnimationComponent::kAuthoredFrame) {
        ImGui::TextDisabled("stopped - the sprite is showing its own image");
    } else {
        ImGui::Text("frame %zu of %zu: '%.*s'", animation.CurrentFrame() + 1, resolved,
                    static_cast<int>(animation.CurrentFrameName().size()),
                    animation.CurrentFrameName().data());
    }

    if (animation.IsFinished()) {
        ImGui::TextDisabled("finished, holding its last frame");
    }

    if (ImGui::Button(animation.IsPlaying() ? "Pause" : "Play")) {
        if (animation.IsPlaying()) {
            animation.Pause();
        } else {
            animation.Play();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Restart")) {
        animation.Restart();
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) {
        animation.Stop();
    }

    // NOTHING HERE CALLS MarkDirty, and that is deliberate: these four buttons
    // change what is happening in a running game, not what the scene file
    // says. Stop puts the sprite's authored picture back, which is the state
    // the file already describes.
    ImGui::TextDisabled("these act on the running game and are not saved");
}

// ---------------------------------------------------------------------------
//  Colliders
// ---------------------------------------------------------------------------

// Collects every layer name mentioned anywhere in the scene, plus a few common
// ones, so the checkbox list below offers something sensible without anybody
// having to register layer names in advance.
//
// std::set keeps them sorted and refuses duplicates, which is exactly what is
// wanted here.
std::set<std::string> GatherLayerNames(eng::Scene& scene) {
    std::set<std::string> names{"Default", "Player", "Enemy", "Pickup", "World"};

    scene.ForEach([&](eng::Entity& entity) {
        entity.ForEachComponent([&](eng::Component& component) {
            if (auto* collider = dynamic_cast<eng::ColliderComponent*>(&component)) {
                names.insert(collider->Layer());
                for (const std::string& other : collider->CollidesWith()) {
                    if (other != eng::kCollisionLayerAll) {
                        names.insert(other);
                    }
                }
            }
        });
    });
    return names;
}

// ---------------------------------------------------------------------------
//  The outline of a polygon or an edge collider, as an editable list.
//
//  A TEMPLATE over the two collider types rather than two near-identical
//  functions, because they offer the same three operations - Points,
//  SetPoints and a maximum - and the drift between two copies of a list editor
//  is exactly the kind nobody notices until one of them loses a point.
//
//  THE WHOLE LIST IS REPLACED on any change rather than edited in place. That
//  is what routes every edit through SetPoints, which is where the count is
//  checked and clamped - so the panel cannot produce a shape the collision
//  system refuses.
// ---------------------------------------------------------------------------
// `minPoints` IS PER TYPE and it is not decoration. A polygon needs three -
// Deserialize already refuses fewer - and an edge chain needs two. This used
// to hard-code the edge chain's two for both, so a polygon could be cut to a
// pair of points: SetPoints accepts them, WorldShapes then produces nothing
// at all, and the collider sits there Enabled while colliding with nothing,
// firing no events, invisible to every query and drawing no gizmo, under a
// panel reading "NOT CONVEX". It does not even round-trip - reload the scene
// and Deserialize refuses the file.
template <typename T>
void DrawPointList(T& collider, int maxPoints, int minPoints) {
    std::vector<eng::Vec2> points = collider.Points();
    bool                   changed = false;

    // Deferred, for the same reason the Assets panel defers a move: removing
    // an entry while the loop that draws it is still running would read past
    // the end of the list.
    int removeAt = -1;

    for (std::size_t i = 0; i < points.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));

        float xy[2] = {points[i].x, points[i].y};
        if (ImGui::DragFloat2("##point", xy, 0.5f)) {
            points[i] = eng::Vec2{xy[0], xy[1]};
            changed    = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            removeAt = static_cast<int>(i);
        }
        ImGui::PopID();
    }

    if (removeAt >= 0 &&
        points.size() > static_cast<std::size_t>(std::max(minPoints, 1))) {
        points.erase(points.begin() + removeAt);
        changed = true;
    }

    if (static_cast<int>(points.size()) < maxPoints && ImGui::SmallButton("+ point")) {
        // The new point goes just past the last one rather than on top of it.
        // Two points in the same place have no direction between them, and a
        // shape with a zero-length edge is one the maths has to special-case.
        const eng::Vec2 last = points.empty() ? eng::Vec2{0.0f, 0.0f} : points.back();
        points.push_back(last + eng::Vec2{8.0f, 0.0f});
        changed = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%d of %d points (minimum %d)",
                        static_cast<int>(points.size()), maxPoints, minPoints);

    if (changed) {
        collider.SetPoints(std::move(points));
        EditorState::Get().MarkDirty();
    }
}

// ---------------------------------------------------------------------------
//  Rigidbody2D
// ---------------------------------------------------------------------------
void DrawRigidbody(eng::Rigidbody2DComponent& body) {
    int type = 0;
    switch (body.BodyType()) {
        case eng::RigidbodyType::Dynamic:   type = 0; break;
        case eng::RigidbodyType::Kinematic: type = 1; break;
        case eng::RigidbodyType::Static:    type = 2; break;
    }
    if (ImGui::Combo("Body type", &type, "Dynamic\0Kinematic\0Static\0")) {
        body.SetBodyType(type == 0   ? eng::RigidbodyType::Dynamic
                         : type == 1 ? eng::RigidbodyType::Kinematic
                                     : eng::RigidbodyType::Static);
        EditorState::Get().MarkDirty();
    }

    // WHAT EACH ONE MEANS, on screen rather than in a manual. Picking the
    // wrong body type is the most common physics mistake there is, and the
    // moment somebody is choosing is the moment the explanation is worth
    // having.
    switch (body.BodyType()) {
        case eng::RigidbodyType::Dynamic:
            ImGui::TextDisabled("gravity and forces move it; collisions push it about");
            break;
        case eng::RigidbodyType::Kinematic:
            ImGui::TextDisabled("moves only when told to, and pushes dynamic things "
                                "out of the way - a lift, a platform, a character");
            break;
        case eng::RigidbodyType::Static:
            ImGui::TextDisabled("never moves; the same as having no rigidbody at all");
            break;
    }

    const bool dynamic = body.BodyType() == eng::RigidbodyType::Dynamic;
    if (!dynamic) {
        ImGui::BeginDisabled();
    }

    bool autoMass = body.UseAutoMass();
    if (ImGui::Checkbox("Auto mass", &autoMass)) {
        body.SetUseAutoMass(autoMass);
        EditorState::Get().MarkDirty();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(from the collider's area x its density)");

    if (autoMass) {
        ImGui::BeginDisabled();
    }
    float mass = body.Mass();
    if (ImGui::DragFloat("Mass", &mass, 0.1f, 0.0001f, 100000.0f)) {
        body.SetMass(mass);
        EditorState::Get().MarkDirty();
    }
    if (autoMass) {
        ImGui::EndDisabled();
        ImGui::TextDisabled("working out to %.3f", static_cast<double>(body.Mass()));
    }

    float gravityScale = body.GravityScale();
    if (ImGui::DragFloat("Gravity scale", &gravityScale, 0.05f, -10.0f, 10.0f)) {
        body.SetGravityScale(gravityScale);
        EditorState::Get().MarkDirty();
    }
    ImGui::TextDisabled("0 floats, 1 is normal, 2 falls twice as hard");

    float linearDrag = body.LinearDrag();
    if (ImGui::DragFloat("Linear drag", &linearDrag, 0.05f, 0.0f, 100.0f)) {
        body.SetLinearDrag(linearDrag);
        EditorState::Get().MarkDirty();
    }

    float angularDrag = body.AngularDrag();
    if (ImGui::DragFloat("Angular drag", &angularDrag, 0.01f, 0.0f, 100.0f)) {
        body.SetAngularDrag(angularDrag);
        EditorState::Get().MarkDirty();
    }

    bool freeze = body.FreezeRotation();
    if (ImGui::Checkbox("Freeze rotation", &freeze)) {
        body.SetFreezeRotation(freeze);
        EditorState::Get().MarkDirty();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(nearly always what a character wants)");

    if (!dynamic) {
        ImGui::EndDisabled();
    }

    // THE LIVE VALUES, which are not settings and are not saved. Worth showing
    // because "why is it drifting?" is answered by looking at a velocity that
    // is not quite zero, and there is nowhere else to see it.
    ImGui::SeparatorText("Right now");
    const eng::Vec2 velocity = body.Velocity();
    ImGui::Text("velocity  %.2f, %.2f", static_cast<double>(velocity.x),
                static_cast<double>(velocity.y));
    ImGui::Text("spin      %.3f rad/s",
                static_cast<double>(body.AngularVelocity()));
}

// ---------------------------------------------------------------------------
//  TilemapComponent
//
//  Deliberately NOT a cell editor. Cells are painted in the Scene view with
//  the Tile Palette; a grid of numbers here would be a second, worse way of
//  doing the same thing. What belongs here is everything ABOUT the map that
//  is not a cell: how big a cell is, which layer it draws on, and which
//  materials it uses.
// ---------------------------------------------------------------------------
void DrawTilemap(eng::TilemapComponent& map) {
    ImGui::Text("%zu tile(s) in %zu chunk(s)", map.TileCount(), map.ChunkCount());

    eng::TileCoord lo{};
    eng::TileCoord hi{};
    if (map.TileBounds(lo, hi)) {
        ImGui::TextDisabled("painted from (%d, %d) to (%d, %d)", lo.x, lo.y, hi.x, hi.y);
    } else {
        ImGui::TextDisabled("nothing painted yet - use the Tile Palette");
    }

    int layer = map.Layer();
    if (ImGui::DragInt("Layer", &layer, 0.2f, -1000, 1000)) {
        map.SetLayer(layer);
        EditorState::Get().MarkDirty();
    }
    ImGui::TextDisabled("shared with sprites: a NEGATIVE layer puts the map behind them");

    eng::Color    tint = map.Tint();
    float         rgba[4] = {tint.r / 255.0f, tint.g / 255.0f, tint.b / 255.0f,
                             tint.a / 255.0f};
    if (ImGui::ColorEdit4("Tint", rgba)) {
        map.SetTint(eng::Color{static_cast<unsigned char>(rgba[0] * 255.0f),
                               static_cast<unsigned char>(rgba[1] * 255.0f),
                               static_cast<unsigned char>(rgba[2] * 255.0f),
                               static_cast<unsigned char>(rgba[3] * 255.0f)});
        EditorState::Get().MarkDirty();
    }

    // ---- cell size ---------------------------------------------------------
    const eng::Vec2 cellSize = map.CellSize();
    bool            explicitSize = map.HasExplicitCellSize();

    if (ImGui::Checkbox("Set cell size by hand", &explicitSize)) {
        // Unticking goes back to the tile's own size, which is what a map
        // drawn at the size its art was drawn at wants.
        map.SetCellSize(explicitSize ? cellSize : eng::Vec2{0.0f, 0.0f});
        EditorState::Get().MarkDirty();
    }

    if (map.HasExplicitCellSize()) {
        float size[2] = {cellSize.x, cellSize.y};
        if (ImGui::DragFloat2("Cell size", size, 0.5f, 1.0f, 4096.0f)) {
            map.SetCellSize(eng::Vec2{size[0], size[1]});
            EditorState::Get().MarkDirty();
        }
    } else {
        ImGui::Text("Cell size: %.0f x %.0f", static_cast<double>(cellSize.x),
                    static_cast<double>(cellSize.y));
        ImGui::TextDisabled("taken from the first tile in the palette");
    }

    // ---- the palette -------------------------------------------------------
    ImGui::SeparatorText("Palette");
    ImGui::TextDisabled("a cell stores a position in this list, not a path");

    if (map.Palette().empty()) {
        ImGui::TextDisabled("empty - painting adds to it");
    }

    for (std::size_t i = 0; i < map.Palette().size(); ++i) {
        const std::string&    path  = map.Palette()[i];
        const eng::TileAsset* asset = map.AssetFor(static_cast<eng::TileId>(i + 1));

        ImGui::PushID(static_cast<int>(i));
        if (asset != nullptr) {
            ImGui::Text("%zu. %s", i + 1, path.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("(%dpx%s%s)", asset->TileSize(),
                                asset->IsSolid() ? ", solid" : "",
                                asset->IsConnector() ? ", connector" : "");
        } else {
            // Named but not loadable. Said in red, because every cell using
            // this entry is occupied as far as the map is concerned and
            // invisible as far as the screen is - which is a confusing pair of
            // symptoms to be left to work out.
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f),
                               "%zu. %s  - WILL NOT LOAD", i + 1, path.c_str());
        }
        ImGui::PopID();
    }

    if (!map.Palette().empty()) {
        if (ImGui::Button("Drop unused entries")) {
            const std::size_t dropped = map.CompactPalette();
            EditorState::Get().MarkDirty();
            ENGINE_LOG_INFO(eng::Channels::kTilemap,
                            "tilemap palette compacted: {} entr{} dropped", dropped,
                            dropped == 1 ? "y" : "ies");
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload tiles")) {
            // Re-reads every .otile the map uses, so editing one in a text
            // editor and coming back shows the change.
            for (const std::string& path : map.Palette()) {
                std::string error;
                if (!eng::TileLibrary::Reload(path, error)) {
                    ENGINE_LOG_ERROR(eng::Channels::kTilemap,
                                     "could not reload '{}': {}", path, error);
                }
            }
            map.RefreshPalette();

            // ==========================================================
            //  AND THE COLLIDER HAS TO BE TOLD, which is the whole point
            //  of pressing this.
            //
            //  TilemapColliderComponent rebuilds itself when the map's
            //  REVISION changes or its cell size does. Reloading a .otile
            //  changes neither: no cell was painted, and the tile is the
            //  same size. So the collider kept its cache and a tile
            //  changed from "collision": "None" to "Grid" left every cell
            //  using it walk-through - and the reverse left a field of
            //  invisible walls.
            //
            //  TilemapCollider.h documents Invalidate() as existing for
            //  precisely this: "the editor calls it after a .otile is
            //  edited, because a tile changing from decoration to solid
            //  changes which cells are solid without any cell changing."
            //  Nothing called it.
            // ==========================================================
            if (eng::Entity* owner = map.Owner(); owner != nullptr) {
                if (auto* solid = owner->Find<eng::TilemapColliderComponent>()) {
                    solid->Invalidate();
                }
            }
        }
    }

    ImGui::SeparatorText("Danger");
    if (ImGui::Button("Clear every cell")) {
        ImGui::OpenPopup("confirmclear");
    }
    if (ImGui::BeginPopupModal("confirmclear", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Erase all %zu tile(s) in this map?", map.TileCount());

        // It said "There is no undo." until there was one. The cells are part
        // of the scene document, so this is one Ctrl+Z like any other edit -
        // which is also why the question is still asked: erasing a few
        // thousand cells by accident is worth one confirmation even when it
        // can be taken back.
        ImGui::TextDisabled("Ctrl+Z will bring them back.");
        if (ImGui::Button("Erase")) {
            map.Clear();
            EditorState::Get().MarkDirty();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void DrawCollider(eng::ColliderComponent& collider, eng::Scene& scene) {
    bool enabled = collider.Enabled();
    if (ImGui::Checkbox("Enabled", &enabled)) {
        collider.SetEnabled(enabled);
        EditorState::Get().MarkDirty();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(switched off it takes part in nothing - no events, no "
                        "pushing, invisible to queries)");

    bool trigger = collider.IsTrigger();
    if (ImGui::Checkbox("Trigger", &trigger)) {
        collider.SetTrigger(trigger);
        EditorState::Get().MarkDirty();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(reports overlaps but does not block movement)");

    // SPELLED OUT, because this is the one setting that silently changes which
    // hook a script needs. See physics/Collider.h.
    if (trigger) {
        ImGui::TextDisabled("sends OnTriggerEnter / Stay / Exit - NOT OnCollision...");
    } else {
        ImGui::TextDisabled("sends OnCollisionEnter / Stay / Exit");
    }

    float offset[2] = {collider.Offset().x, collider.Offset().y};
    if (ImGui::DragFloat2("Offset", offset, 0.5f)) {
        collider.SetOffset(eng::Vec2{offset[0], offset[1]});
        EditorState::Get().MarkDirty();
    }

// The shape-specific fields. Which ones depend on what this actually is.
    switch (collider.Shape()) {
        case eng::ColliderShape::Box: {
            auto& box = static_cast<eng::BoxColliderComponent&>(collider);
            float half[2] = {box.HalfExtents().x, box.HalfExtents().y};
            if (ImGui::DragFloat2("Half size", half, 0.5f, 0.0f, 10000.0f)) {
                box.SetHalfExtents(eng::Vec2{half[0], half[1]});
                EditorState::Get().MarkDirty();
            }
            ImGui::TextDisabled("half the width and half the height");

            float edge = box.EdgeRadius();
            if (ImGui::DragFloat("Edge radius", &edge, 0.1f, 0.0f, 1000.0f)) {
                box.SetEdgeRadius(edge);
                EditorState::Get().MarkDirty();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(rounds the corners)");
            break;
        }

        case eng::ColliderShape::Circle: {
            auto& circle = static_cast<eng::CircleColliderComponent&>(collider);
            float radius = circle.Radius();
            if (ImGui::DragFloat("Radius", &radius, 0.5f, 0.0f, 10000.0f)) {
                circle.SetRadius(radius);
                EditorState::Get().MarkDirty();
            }
            break;
        }

        case eng::ColliderShape::Capsule: {
            auto& capsule = static_cast<eng::CapsuleColliderComponent&>(collider);
            float size[2] = {capsule.Size().x, capsule.Size().y};
            if (ImGui::DragFloat2("Size", size, 0.5f, 0.0f, 10000.0f)) {
                capsule.SetSize(eng::Vec2{size[0], size[1]});
                EditorState::Get().MarkDirty();
            }

            int direction = capsule.Direction() == eng::CapsuleDirection::Vertical ? 0 : 1;
            if (ImGui::Combo("Direction", &direction, "Vertical\0Horizontal\0")) {
                capsule.SetDirection(direction == 0 ? eng::CapsuleDirection::Vertical
                                                    : eng::CapsuleDirection::Horizontal);
                EditorState::Get().MarkDirty();
            }
            ImGui::TextDisabled("the round ends are on the long axis; as wide as it is "
                                "tall makes a circle");
            break;
        }

        case eng::ColliderShape::Polygon: {
            auto& polygon = static_cast<eng::PolygonColliderComponent&>(collider);
            DrawPointList(polygon, eng::ConvexShape::kMaxPoints, 3);

            // SAID OUT LOUD, because a dented outline still collides - as its
            // convex hull - and the shape on screen then differs from the
            // shape being tested with nothing to show why.
            if (!polygon.IsConvex()) {
                ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1.0f),
                                   "NOT CONVEX - this collides as the hull of these "
                                   "points, not as drawn.");
                ImGui::TextDisabled("Split it into several colliders instead.");
            }
            break;
        }

        case eng::ColliderShape::Tilemap: {
            auto& tiles = static_cast<eng::TilemapColliderComponent&>(collider);

            ImGui::TextWrapped(
                "This collider has no shape of its own: it is the TilemapComponent "
                "beside it. Cells whose tile says \"collision\": \"Grid\" are solid, "
                "and the rest are walked through.");

            const std::size_t cells = tiles.SolidCellCount();
            const std::size_t boxes = tiles.BoxCount();
            if (boxes > 0) {
                ImGui::Text("%zu solid cell(s) merged into %zu box(es)", cells, boxes);
                ImGui::TextDisabled(
                    "merged %.0f to 1 - a flat floor is ONE box, so nothing can catch "
                    "on a join between two of them",
                    static_cast<double>(cells) / static_cast<double>(boxes));
            } else if (cells > 0) {
                ImGui::TextDisabled("%zu solid cell(s), not yet merged", cells);
            } else {
                ImGui::TextDisabled("nothing here is solid yet");
            }

            if (ImGui::Button("Rebuild")) {
                // Only needed when a .otile has been edited: changing a tile
                // from decoration to solid changes which cells are solid
                // without any cell having changed, so the map's revision does
                // not move and nothing would otherwise notice.
                tiles.Invalidate();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Painting rebuilds this automatically. Use it after "
                                  "editing a .otile file's \"collision\" setting.");
            }
            break;
        }

        case eng::ColliderShape::Edge: {
            auto& edge = static_cast<eng::EdgeColliderComponent&>(collider);
            DrawPointList(edge, eng::EdgeColliderComponent::kMaxEdgePoints, 2);

            float radius = edge.EdgeRadius();
            if (ImGui::DragFloat("Edge radius", &radius, 0.1f, 0.0f, 1000.0f)) {
                edge.SetEdgeRadius(radius);
                EditorState::Get().MarkDirty();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(gives the line thickness)");

            bool loop = edge.Loop();
            if (ImGui::Checkbox("Loop", &loop)) {
                edge.SetLoop(loop);
                EditorState::Get().MarkDirty();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(joins the last point back to the first)");
            break;
        }
    }

    ImGui::SeparatorText("Surface");

    eng::PhysicsMaterial2D material = collider.Material();
    bool                   materialChanged = false;

    materialChanged |= ImGui::DragFloat("Friction", &material.friction, 0.01f, 0.0f, 2.0f);
    ImGui::SameLine();
    ImGui::TextDisabled("(0 is ice)");

    materialChanged |=
        ImGui::DragFloat("Bounciness", &material.bounciness, 0.01f, 0.0f, 1.0f);
    ImGui::SameLine();
    ImGui::TextDisabled("(0 is a beanbag, 1 a superball)");

    if (materialChanged) {
        collider.SetMaterial(material);
        EditorState::Get().MarkDirty();
    }

    float density = collider.Density();
    if (ImGui::DragFloat("Density", &density, 0.05f, 0.0001f, 1000.0f)) {
        collider.SetDensity(density);
        EditorState::Get().MarkDirty();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(only used when a rigidbody has Auto mass on)");

    // NEITHER OF THE FIRST TWO DOES ANYTHING WITHOUT A RIGIDBODY, and a
    // slider that silently has no effect is worse than no slider. Said here
    // rather than left to be discovered.
    if (collider.AttachedRigidbody() == nullptr && !collider.IsTrigger()) {
        ImGui::TextDisabled("no Rigidbody2D on this entity, so nothing here is pushed "
                            "- friction and bounciness need something to act on.");
    }

    ImGui::SeparatorText("Layers");

    char layer[64];
    std::snprintf(layer, sizeof(layer), "%s", collider.Layer().c_str());
    if (ImGui::InputText("Layer (what it is)", layer, sizeof(layer),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        collider.SetLayer(layer);
        EditorState::Get().MarkDirty();
    }

    // "Collides with" as a set of checkboxes. Untick one and the collision
    // events for that pairing stop immediately - no rebuild, no restart, which
    // makes it easy to see the layer rules actually working.
    ImGui::TextUnformatted("Collides with:");

    std::vector<std::string> wanted = collider.CollidesWith();
    bool changed = false;

    // "All" is a special entry meaning "everything". When it is ticked the
    // individual boxes are irrelevant, so they are greyed out.
    bool all = std::find(wanted.begin(), wanted.end(), eng::kCollisionLayerAll) !=
               wanted.end();
    if (ImGui::Checkbox("All", &all)) {
        wanted.clear();
        if (all) {
            wanted.emplace_back(eng::kCollisionLayerAll);
        }
        changed = true;
    }

    ImGui::BeginDisabled(all);
    int column = 0;
    for (const std::string& name : GatherLayerNames(scene)) {
        bool on = std::find(wanted.begin(), wanted.end(), name) != wanted.end();
        if (column++ % 3 != 0) {
            ImGui::SameLine();
        }
        // The hidden id after ## keeps two checkboxes with the same visible
        // label apart. See the note about widget identity in Panel.h.
        char id[96];
        std::snprintf(id, sizeof(id), "%s##collideswith", name.c_str());
        if (ImGui::Checkbox(id, &on)) {
            if (on) {
                wanted.push_back(name);
            } else {
                std::erase(wanted, name);
            }
            changed = true;
        }
    }
    ImGui::EndDisabled();

    if (changed) {
        collider.SetCollidesWith(std::move(wanted));
        EditorState::Get().MarkDirty();
    }

    ImGui::TextDisabled("Two colliders are only tested when EACH one's list "
                        "includes the other's layer.");
}

// ---------------------------------------------------------------------------
//  SpinComponent
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
//  CameraComponent
// ---------------------------------------------------------------------------
void DrawCamera(eng::CameraComponent& camera) {
    // WHICH CAMERA IS ACTUALLY LIVE, said first and plainly. With more than
    // one camera in a scene, "why is the Game view not where I put this?" is
    // the question this panel exists to answer.
    if (camera.IsActive()) {
        ImGui::TextColored(ImVec4(0.55f, 0.90f, 0.60f, 1.0f),
                           "This camera is driving the Game view.");
    } else if (!camera.Enabled()) {
        ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.78f, 1.0f),
                           "Disabled - it is ignored entirely.");
    } else if (const eng::CameraComponent* active = eng::CameraSystem::Active()) {
        const eng::Entity* owner = active->Owner();
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f),
                           "NOT in use - '%s' has a higher priority (%d vs %d).",
                           owner != nullptr ? owner->Name().c_str() : "another camera",
                           active->Priority(), camera.Priority());
    }

    float zoom = camera.Zoom();
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::DragFloat("Zoom", &zoom, 0.01f, 0.01f, 50.0f, "%.2fx")) {
        camera.SetZoom(zoom);
        EditorState::Get().MarkDirty();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Bigger is closer in. This is the game's zoom - the Scene "
                          "view has its own and is not affected.");
    }

    int priority = camera.Priority();
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::DragInt("Priority", &priority, 0.2f)) {
        camera.SetPriority(priority);
        EditorState::Get().MarkDirty();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("With several cameras in a scene, the highest priority is "
                          "the one used. Ties go to whichever was found first.");
    }

    bool enabled = camera.Enabled();
    if (ImGui::Checkbox("Enabled", &enabled)) {
        camera.SetEnabled(enabled);
        EditorState::Get().MarkDirty();
    }

    ImGui::Separator();

    // THE POSITION IS THE TRANSFORM'S, and saying so stops somebody looking
    // for a position field that deliberately does not exist.
    ImGui::TextDisabled("Its position is the Transform above - there is no separate");
    ImGui::TextDisabled("camera position. Parent it to something to make it follow.");

    if (eng::CameraSystem::Count() > 1) {
        ImGui::TextDisabled("%zu cameras in this scene.", eng::CameraSystem::Count());
    }
}

// ---------------------------------------------------------------------------
//  The interface components. See engine/include/engine/scene/UI.h.
// ---------------------------------------------------------------------------

// A colour, as four 0-255 channels. ImGui works in 0-1 floats, so this is the
// conversion both ways in one place rather than in each of the eight callers.
bool DrawColorField(const char* label, eng::Color& color) {
    float rgba[4] = {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f,
                     color.a / 255.0f};
    if (!ImGui::ColorEdit4(label, rgba, ImGuiColorEditFlags_AlphaBar)) {
        return false;
    }
    const auto channel = [](float v) {
        const float scaled = v * 255.0f + 0.5f;
        return static_cast<unsigned char>(scaled < 0.0f ? 0.0f
                                          : (scaled > 255.0f ? 255.0f : scaled));
    };
    color = eng::Color{channel(rgba[0]), channel(rgba[1]), channel(rgba[2]),
                       channel(rgba[3])};
    return true;
}

void DrawUIRect(eng::UIRectComponent& rect) {
    bool changed = false;

    // ---- which space ------------------------------------------------------
    int space = (rect.space == eng::UISpace::World) ? 1 : 0;
    if (ImGui::Combo("Space", &space, "Screen\0World\0")) {
        rect.space = (space == 1) ? eng::UISpace::World : eng::UISpace::Screen;
        changed    = true;
    }
    if (rect.space == eng::UISpace::World) {
        ImGui::TextDisabled("positioned by this entity's TRANSFORM, and it");
        ImGui::TextDisabled("scales with the camera - good for a name tag");
    } else {
        ImGui::TextDisabled("positioned in pixels from the anchor, and the");
        ImGui::TextDisabled("camera does not affect it - good for a score");
    }

    // ---- anchor, only when it means something -----------------------------
    //
    // DISABLED rather than hidden in world space, where the transform is in
    // charge and an anchor does nothing. A field that vanishes teaches
    // nothing; one that is visibly unavailable says it does not apply.
    ImGui::BeginDisabled(rect.space == eng::UISpace::World);
    int anchor = static_cast<int>(rect.anchor);
    if (ImGui::Combo("Anchor", &anchor,
                     "TopLeft\0Top\0TopRight\0Left\0Center\0Right\0"
                     "BottomLeft\0Bottom\0BottomRight\0")) {
        rect.anchor = static_cast<eng::UIAnchor>(anchor);
        changed     = true;
    }

    float offset[2] = {rect.offset.x, rect.offset.y};
    if (ImGui::DragFloat2("Offset", offset, 1.0f)) {
        rect.offset = eng::Vec2{offset[0], offset[1]};
        changed     = true;
    }
    ImGui::EndDisabled();

    float size[2] = {rect.size.x, rect.size.y};
    if (ImGui::DragFloat2("Size", size, 1.0f, 1.0f, 4096.0f)) {
        rect.size = eng::Vec2{size[0], size[1]};
        changed   = true;
    }

    float pivot[2] = {rect.pivot.x, rect.pivot.y};
    if (ImGui::DragFloat2("Pivot", pivot, 0.01f, 0.0f, 1.0f)) {
        rect.pivot = eng::Vec2{pivot[0], pivot[1]};
        changed    = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Which point of this object sits at the position above. "
                          "0,0 is its top-left corner and 0.5,0.5 its middle.");
    }

    if (ImGui::DragInt("Sort order", &rect.sortOrder, 0.1f, -100, 100)) {
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Higher draws later, so higher is in front.");
    }

    if (ImGui::Checkbox("Visible", &rect.visible)) {
        changed = true;
    }

    // Where it actually ended up, which is the fastest way to answer "why is
    // my label off the edge of the screen".
    ImGui::TextDisabled("on screen: %.0f,%.0f to %.0f,%.0f", rect.resolvedMin.x,
                        rect.resolvedMin.y, rect.resolvedMax.x, rect.resolvedMax.y);

    if (changed) {
        EditorState::Get().MarkDirty();
    }
}

void DrawUIText(eng::UITextComponent& label) {
    bool changed = false;

    char buffer[512];
    std::snprintf(buffer, sizeof(buffer), "%s", label.text.c_str());
    if (ImGui::InputTextMultiline("Text", buffer, sizeof(buffer),
                                  ImVec2(-1.0f, ImGui::GetTextLineHeight() * 3.0f))) {
        label.text = buffer;
        changed    = true;
    }

    char fontBuffer[256];
    std::snprintf(fontBuffer, sizeof(fontBuffer), "%s", label.fontPath.c_str());
    if (ImGui::InputTextWithHint("Font", "fonts/Something.ttf  (blank = built-in)",
                                 fontBuffer, sizeof(fontBuffer))) {
        label.fontPath = fontBuffer;
        changed        = true;
    }

    // DROPPING A FILE ON IT, which is how every other asset is assigned in
    // this editor - and the only discoverable way to find out that a .ttf
    // goes here at all.
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPayloadFont)) {
            label.fontPath = static_cast<const char*>(payload->Data);
            changed        = true;
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::DragFloat("Size", &label.fontSize, 0.5f, 4.0f, 256.0f, "%.0f px")) {
        changed = true;
    }

    changed |= DrawColorField("Colour", label.color);

    int align = static_cast<int>(label.align);
    if (ImGui::Combo("Align", &align, "Left\0Center\0Right\0")) {
        label.align = static_cast<eng::UIAlign>(align);
        changed     = true;
    }
    if (ImGui::Checkbox("Centre vertically", &label.middle)) {
        changed = true;
    }

    // What actually got used, which is the only way to tell a working font
    // path from one that silently fell back.
    if (const eng::FontRef& font = label.ResolvedFont()) {
        if (font->isFallback) {
            ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f),
                               label.fontPath.empty()
                                   ? "using the built-in 8x8 font"
                                   : "that font did not load - using the built-in one");
        } else {
            ImGui::TextDisabled("%s at %.0fpx", font->path.c_str(), font->pixelHeight);
        }
    }

    if (changed) {
        EditorState::Get().MarkDirty();
    }
}

void DrawUIImage(eng::UIImageComponent& image) {
    bool changed = false;

    // The same slot as a Sprite's, for the same reason - and "(none)" in the
    // picker is a real choice here rather than an empty one: a UI image with
    // no texture is a plain coloured rectangle, which is most of what a
    // layout is made of.
    {
        std::string chosen;
        if (DrawAssetSlot("Texture", AssetSlotKind::Image, image.texturePath,
                          /*broken=*/false,
                          "drag an image here from Assets, or leave it empty for a "
                          "plain rectangle",
                          chosen)) {
            image.texturePath = chosen;
            changed           = true;
        }
        if (image.texturePath.empty()) {
            ImGui::TextDisabled("empty = a plain rectangle in the tint colour");
        }
    }

    changed |= DrawColorField("Tint", image.tint);

    if (ImGui::DragFloat("Outline", &image.outlineThickness, 0.1f, 0.0f, 16.0f,
                         "%.0f")) {
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("0 fills the rectangle. Anything else draws its edge only. "
                          "Ignored when a texture is set.");
    }

    if (changed) {
        EditorState::Get().MarkDirty();
    }
}

void DrawUIButton(eng::UIButtonComponent& button) {
    bool changed = false;

    if (ImGui::Checkbox("Interactable", &button.interactable)) {
        changed = true;
    }

    changed |= DrawColorField("Normal",   button.normalTint);
    changed |= DrawColorField("Hover",    button.hoverTint);
    changed |= DrawColorField("Pressed",  button.pressedTint);
    changed |= DrawColorField("Disabled", button.disabledTint);

    ImGui::Separator();

    // LIVE STATE, which turns "my button does nothing" from a guess into a
    // reading: hover it in the Game view and watch this.
    ImGui::TextDisabled("hovered: %s   pressed: %s",
                        button.IsHovered() ? "yes" : "no",
                        button.IsPressed() ? "yes" : "no");
    ImGui::TextWrapped("These colours tint the UIImage on this same entity, so a "
                       "button needs one. Read it from a script with "
                       "Owner()->Find<eng::UIButtonComponent>()->WasClicked().");

    if (changed) {
        EditorState::Get().MarkDirty();
    }
}

void DrawUISlider(eng::UISliderComponent& slider) {
    bool changed = false;

    if (ImGui::DragFloat("Value", &slider.value, 0.01f, slider.minValue,
                         slider.maxValue)) {
        changed = true;
    }
    if (ImGui::DragFloat("Min", &slider.minValue, 0.1f)) { changed = true; }
    if (ImGui::DragFloat("Max", &slider.maxValue, 0.1f)) { changed = true; }
    if (ImGui::Checkbox("Interactable", &slider.interactable)) { changed = true; }

    changed |= DrawColorField("Track", slider.trackColor);
    changed |= DrawColorField("Fill",  slider.fillColor);
    changed |= DrawColorField("Knob",  slider.knobColor);

    if (slider.maxValue <= slider.minValue) {
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f),
                           "max is not above min, so this cannot be dragged");
    }

    if (changed) {
        EditorState::Get().MarkDirty();
    }
}

void DrawSpin(eng::SpinComponent& spin) {
    // Edited in degrees, stored in radians - same reason as the transform's
    // rotation field above.
    float degrees = spin.RadiansPerSecond() * eng::kRadToDeg;
    if (ImGui::DragFloat("Degrees/sec", &degrees, 1.0f, -720.0f, 720.0f, "%.1f")) {
        spin.SetRadiansPerSecond(degrees * eng::kDegToRad);
        EditorState::Get().MarkDirty();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("0")) {
        spin.SetRadiansPerSecond(0.0f);
        EditorState::Get().MarkDirty();
    }

    ImGui::TextDisabled("negative numbers spin the other way");
    ImGui::TextWrapped("Anything parented to this entity ORBITS it, because a child's "
                       "position in the world is worked out through its parent. There "
                       "is no orbit code anywhere - drag this and watch.");
}

// ---------------------------------------------------------------------------
//  ScriptComponent
// ---------------------------------------------------------------------------
// The script's name, drawn as a link that opens the file it came from in
// whatever this machine uses for .cpp files - the same thing double-clicking
// it in the Assets panel does. See ShellOpen.h.
//
// WHY A LOOKUP IS NEEDED AT ALL: a ScriptComponent stores a name, not a path,
// so the file has to be found again. That walks the assets tree, which is why
// it happens on the CLICK rather than every frame while the link is merely on
// screen - see ScriptPathFromName.
//
// The consequence is that the link is drawn without knowing whether there is
// a file behind it. Reporting a miss when somebody asks is honest and costs
// nothing; scanning the disk sixty times a second to grey out a label would
// cost a great deal.
void DrawScriptNameLink(const eng::ScriptComponent& script, ImVec4 colour) {
    ImGui::PushStyleColor(ImGuiCol_TextLink, colour);
    const bool clicked = ImGui::TextLink(script.ScriptName().c_str());
    ImGui::PopStyleColor();

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Open %s in the program this machine uses for that kind "
                          "of file", script.ScriptName().c_str());
    }
    if (!clicked) {
        return;
    }

    const std::string path = ScriptPathFromName(script.ScriptName());
    if (path.empty()) {
        // AT ERROR, not Warning: the project's log threshold discards
        // warnings, and a link that does nothing and says nothing is the
        // exact failure this feature exists to avoid. Same reasoning as
        // ShellOpen.h.
        ENGINE_LOG_ERROR(eng::Channels::kEditor,
                         "no file for the script '{}' was found under assets/ - a "
                         "script is opened by looking for a file named after it",
                         script.ScriptName());
        return;
    }

    std::string error;
    if (OpenExternally(path, error)) {
        ENGINE_LOG_INFO(eng::Channels::kEditor, "opened {}", path);
    } else {
        ENGINE_LOG_ERROR(eng::Channels::kEditor, "could not open '{}': {}", path, error);
    }
}

// ---------------------------------------------------------------------------
//  A script's variables.
//
//  The script says which ones it has - see engine/scene/ScriptFields.h for why
//  it has to say rather than be asked - and everything here is drawing them.
//
//  ==========================================================================
//  THE LABEL IS THE NAME FROM THE CODE, NOT A TIDIED-UP VERSION OF IT.
//
//  Unity turns `maxSpeed` into "Max Speed", which reads better and is the
//  wrong trade here. That name is also the KEY IN THE SCENE FILE, and this
//  project's scenes are meant to be opened and edited by hand - so a label
//  that does not match what is in the file would mean looking up the
//  translation every time. `maxSpeed` on screen and "maxSpeed" in the file.
//
//  ==========================================================================
//  WHAT THE OVERRIDE MARK IS FOR
//
//  A variable somebody changed is saved in the scene; one they left alone is
//  not, and takes whatever the code says. Those two look identical on screen -
//  both are just a number - so an overridden one gets a `revert` button beside
//  it. That is both the indicator and the way out.
// ---------------------------------------------------------------------------

// The longest text a string variable can be edited with here. Generous enough
// for any name or path in this project; the case that exceeds it is handled
// rather than truncated. See DrawStringField.
constexpr int kScriptStringMax = 256;

// How fast a drag box moves, taken from the number already in it.
//
// One fixed step cannot serve both a 0.2 blend and a 2000-pixel distance: at
// 0.1 per pixel the first is unusable and the second takes half a minute to
// cross. Scaling by the value gives fine control near zero and coarse control
// far out, which is what anybody would pick by hand for each field.
//
// The floor matters: without it a variable sitting at exactly 0 would have a
// step of 0 and the drag would be dead.
float DragStepFor(float value) {
    return std::max(0.01f, std::abs(value) * 0.01f);
}

// Text is the one type that cannot be handed to ImGui as a pointer, because
// ImGui edits a char buffer and the variable is a std::string.
// Which asset slot a string variable's marker asks for, if any.
//
// `// [image]` or `// [animation]` on a std::string turns its text box into
// the slot a Sprite's texture gets. Anything else in the marker is a required
// TYPE for an entity reference and means nothing here, so the box stays a box.
//
// ===========================================================================
//  THE LIST OF ACCEPTED SPELLINGS IS THE ENGINE'S, NOT THIS FILE'S.
//
//  This used to keep its own copy of it - image/texture/sprite/animation -
//  which agreed with eng::ScriptFields::AddAsset's copy by hand and by luck.
//  Two lists, one deciding whether to warn and one deciding whether to draw,
//  with nothing keeping them in step: add a kind to one and a marker either
//  draws a slot after being warned about or is accepted silently and then
//  drawn as text.
//
//  All that is left here is the mapping onto this panel's own widget, and the
//  switch has NO `default:` - so a kind added to eng::AssetFieldKind is a
//  warning on this line rather than a silent text box in the Inspector.
//
//  See the note above AssetFieldKind in engine/scene/ScriptFields.h.
// ===========================================================================
bool AssetSlotKindForMarker(const std::string& marker, AssetSlotKind& outKind) {
    switch (eng::AssetFieldKindFor(marker)) {
        case eng::AssetFieldKind::Image:
            outKind = AssetSlotKind::Image;
            return true;
        case eng::AssetFieldKind::Animation:
            outKind = AssetSlotKind::Animation;
            return true;
        case eng::AssetFieldKind::None:
            break;
    }
    return false;
}

bool DrawStringField(const eng::ScriptField& field) {
    std::string* text = field.AsString();
    if (text == nullptr) {
        return false;
    }

    // ---- an asset path, when the variable says it is one ------------------
    if (AssetSlotKind kind{}; AssetSlotKindForMarker(field.requiredType, kind)) {
        std::string chosen;

        // THIS USED TO PASS false UNCONDITIONALLY, so a marked string NEVER
        // painted red however broken it was - an image whose file was gone and
        // a clip renamed in the slicer both looked fine. The component's own
        // slot has always done better. A typed eng::AnimationRef or
        // eng::ImageRef is the better answer now, but the older spelling still
        // has to tell the truth: the two must not disagree about one path.
        //
        // AND eng::ImageExists RATHER THAN FileSystem::Exists FOR AN IMAGE.
        // The picker a few lines below offers every frame of every sliced
        // sheet, so choosing one puts "sheet.png#frame" in the variable -
        // which is not a file, so Exists said no and a reference picked from
        // the engine's own list painted red. ImageExists resolves it the way
        // the loader does, which is the only answer that cannot disagree with
        // what you see on screen.
        const bool broken = !text->empty() &&
                            (kind == AssetSlotKind::Animation
                                 ? !eng::SheetHasAnimation(*text)
                                 : !eng::ImageExists(*text));

        if (DrawAssetSlot(field.name.c_str(), kind, *text, broken,
                          kind == AssetSlotKind::Animation
                              ? "drag an animation here from Assets - expand a "
                                "sheet's row to find them - or click to choose"
                              : "drag an image here from Assets, or click to choose",
                          chosen)) {
            *text = chosen;
            return true;
        }
        return false;
    }

    // ======================================================================
    //  LONGER THAN THE BUFFER: SHOWN, NOT EDITED.
    //
    //  Copying it in would truncate it, and the first keystroke would then
    //  write the short version back over the variable - deleting the rest of
    //  somebody's text with nothing on screen having warned them. Refusing to
    //  edit it is the only safe option, and the file is still there.
    // ======================================================================
    if (text->size() >= static_cast<std::size_t>(kScriptStringMax)) {
        ImGui::BeginDisabled();
        char preview[80];
        std::snprintf(preview, sizeof(preview), "%.60s...", text->c_str());
        ImGui::InputText(field.name.c_str(), preview, sizeof(preview),
                         ImGuiInputTextFlags_ReadOnly);
        ImGui::EndDisabled();
        ImGui::TextDisabled("%zu characters - too long to edit here, so it is shown "
                            "read-only. Change it in the scene file.", text->size());
        return false;
    }

    char buffer[kScriptStringMax];
    std::snprintf(buffer, sizeof(buffer), "%s", text->c_str());
    const bool typed = ImGui::InputText(field.name.c_str(), buffer, sizeof(buffer));

    // ======================================================================
    //  AND IT TAKES A DROP, whatever kind of asset it is.
    //
    //  A plain string variable is very often a path - a scene to load, a
    //  prefab to spawn by name, a sound - and before this the only way to fill
    //  one in was to read the path off the Assets panel and type it back.
    //  Every kind is accepted because the variable is a string: nothing here
    //  knows which kind it wanted, and refusing the drop would leave somebody
    //  holding exactly the thing they meant.
    //
    //  A variable that says what it holds - `// [image]` - gets the proper
    //  slot above instead, with a name and a picker.
    // ======================================================================
    if (ImGui::BeginDragDropTarget()) {
        for (const char* payloadId : kAllAssetPayloads) {
            if (const ImGuiPayload* payload =
                    ImGui::AcceptDragDropPayload(payloadId)) {
                *text = static_cast<const char*>(payload->Data);
                ImGui::EndDragDropTarget();
                return true;
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (typed) {
        *text = buffer;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
//  A reference to another entity.
//
//  THE GESTURE IS DRAG FROM THE HIERARCHY, which is the one people arrive
//  already knowing, and the Hierarchy was already a drag source before any of
//  this existed - see kPayloadEntity. The button is also a picker, because
//  dragging across a docked layout to a panel you cannot see both ends of is
//  a worse experience than a list.
//
//  ==========================================================================
//  WHAT IT SHOWS WHEN SOMETHING IS WRONG, which is most of the value here.
//
//  A reference holds a NAME, so three different things can be true and they
//  do not look alike:
//
//    nothing set           "(none)", greyed - the ordinary starting state
//    set and resolving     the name, plainly
//    set and NOT resolving the name IN RED - deleted, or renamed in a build
//                          where this script was not compiled in
//    resolving but wrong   the name, plus amber: it is there, and it does not
//                          carry the thing this reference asked for
//
//  The last one is the one no other editor tells you about until the game
//  does nothing. It costs one lookup per frame to answer.
// ---------------------------------------------------------------------------

// Declared early because the reference field below draws SEVERAL items - a
// button, a label, and sometimes a line of red - so the caller's "attach a
// tooltip to whatever was just drawn" would land on the wrong one. It calls
// this itself, against the button, and DrawScriptFields knows to skip it.
void DrawFieldTooltip(const eng::ScriptComponent& script,
                      const eng::ScriptField& field);

// Every entity that would satisfy this reference, newest-looking first is not
// worth arranging - the Hierarchy's order is what somebody expects.
void CollectCandidates(eng::Scene& scene, const std::string& requiredType,
                       std::vector<std::string>& into) {
    into.clear();
    scene.ForEach([&requiredType, &into](eng::Entity& entity) {
        if (requiredType.empty() || eng::EntityHasNamed(entity, requiredType)) {
            into.push_back(entity.Name());
        }
    });
}

// A one-line summary of what is attached, for the picker rows: "Sprite,
// Rigidbody2D, PlayerMovement". It is what tells two entities called Enemy1
// and Enemy2 apart when you are looking for the one with the script on it.
std::string DescribeAttachments(const eng::Entity& entity) {
    std::string out;
    for (std::size_t i = 0; i < entity.ComponentCount(); ++i) {
        const eng::Component* component = entity.ComponentAt(i);
        if (component == nullptr) {
            continue;
        }
        std::string name(component->TypeName());

        // A script is named by its SCRIPT, not by "ScriptComponent" - four
        // identical entries would say nothing about an entity carrying four
        // scripts, which is exactly when this summary is needed.
        if (name == eng::ScriptComponent::kTypeName) {
            name = static_cast<const eng::ScriptComponent*>(component)->ScriptName();
            if (name.empty()) {
                name = "Script";
            }
        } else if (name.ends_with("Component")) {
            // "SpriteComponent" -> "Sprite". The suffix is on every one of
            // them, so it distinguishes nothing and costs a third of the line.
            name.resize(name.size() - 9);
        }

        if (!out.empty()) {
            out += ", ";
        }
        out += name;
    }
    return out;
}

bool DrawEntityRefField(const eng::ScriptComponent& script,
                        const eng::ScriptField& field) {
    eng::EntityRef* ref = field.AsEntityRef();
    if (ref == nullptr) {
        return false;
    }

    eng::Scene&       scene  = eng::Engine::Get().GetScene();
    const eng::Entity* target = ref->Target();
    const bool        missing = ref->IsSet() && target == nullptr;
    const bool        wrongKind =
        target != nullptr && !field.requiredType.empty() &&
        !eng::EntityHasNamed(*target, field.requiredType);

    bool changed = false;

    // ---- the button, which is the current value and the picker -----------
    const char* shown = ref->IsSet() ? ref->TargetName().c_str() : "(none)";

    if (missing) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.40f, 1.0f));
    } else if (!ref->IsSet()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    }

    // Sized so the row lines up with the drag boxes above it, and so there is
    // a target big enough to drop onto rather than a word.
    const bool pick = ImGui::Button(shown, ImVec2(ImGui::CalcItemWidth(), 0.0f));

    if (missing || !ref->IsSet()) {
        ImGui::PopStyleColor();
    }

    // AGAINST THE BUTTON, while the button is still the last item drawn.
    DrawFieldTooltip(script, field);

    // ---- dropping an entity on it ----------------------------------------
    //
    // ACCEPTED WHATEVER IT IS, including an entity that does not satisfy the
    // constraint. Refusing the drop would leave somebody holding the thing
    // they meant with no explanation; taking it and saying it is wrong is the
    // same information without the guessing.
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPayloadEntity)) {
            if (payload->DataSize == static_cast<int>(sizeof(eng::EntityId))) {
                eng::EntityId dropped{};
                std::memcpy(&dropped, payload->Data, sizeof(dropped));
                if (const eng::Entity* entity = scene.Get(dropped)) {
                    ref->Set(*entity);
                    changed = true;
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::SameLine();
    ImGui::TextUnformatted(field.name.c_str());

    // ---- the picker ------------------------------------------------------
    if (pick) {
        ImGui::OpenPopup("pick-entity");
    }
    if (ImGui::BeginPopup("pick-entity")) {
        static std::vector<std::string> candidates;
        CollectCandidates(scene, field.requiredType, candidates);

        if (field.requiredType.empty()) {
            ImGui::TextDisabled("any entity");
        } else {
            ImGui::TextDisabled("entities with a %s", field.requiredType.c_str());
        }
        ImGui::Separator();

        if (ImGui::Selectable("(none)", !ref->IsSet())) {
            ref->Clear();
            changed = true;
        }

        if (candidates.empty()) {
            // NAMING THE CONSTRAINT, because an empty list with no explanation
            // reads as a broken picker rather than as an answer.
            ImGui::TextDisabled("nothing in this scene has a %s on it",
                                field.requiredType.c_str());
        }

        for (const std::string& name : candidates) {
            const bool chosen = ref->TargetName() == name;
            if (ImGui::Selectable(name.c_str(), chosen)) {
                ref->Set(name);
                changed = true;
            }
            if (const eng::Entity* entity = scene.Get(scene.Find(name))) {
                const std::string summary = DescribeAttachments(*entity);
                if (!summary.empty() && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", summary.c_str());
                }
            }
        }
        ImGui::EndPopup();
    }

    // ---- what is wrong with it, if anything ------------------------------
    if (missing) {
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f),
                           "nothing in this scene is called '%s'",
                           ref->TargetName().c_str());
    } else if (wrongKind) {
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f),
                           "'%s' has no %s on it, so this will not do anything",
                           ref->TargetName().c_str(), field.requiredType.c_str());
    }

    return changed;
}

// ---------------------------------------------------------------------------
//  A prefab variable: the slot you drag a prefab onto.
//
//  Deliberately NOT a picker as well. An entity reference has one because the
//  candidates are in the scene and the Hierarchy may be scrolled somewhere
//  else; a prefab is a FILE, the Assets panel is always showing files, and
//  dragging it is both shorter and the thing everybody tries first. A list
//  that duplicated the Assets panel badly would be worse than no list.
//
//  IT REPORTS A MISSING FILE, which is the whole reason this is not a text
//  box. A path that no longer names anything is the normal way a spawner
//  breaks - somebody renames the prefab - and the point of giving this its
//  own type is that the breakage is visible while you are building the level
//  rather than the first time the gun fires.
// ---------------------------------------------------------------------------
bool DrawPrefabRefField(const eng::ScriptComponent& script,
                        const eng::ScriptField&     field) {
    eng::PrefabRef* ref = field.AsPrefabRef();
    if (ref == nullptr) {
        return false;
    }

    const bool missing = ref->IsSet() && !eng::FileSystem::Exists(ref->Path());

    bool changed = false;

    // Just the file's own name on the button; the whole path is in the
    // tooltip. "bullet.oprefab" is what somebody is looking for, and
    // "prefabs/enemies/ranged/bullet.oprefab" does not fit in the column.
    std::string shown = "(none)";
    if (ref->IsSet()) {
        const std::string& path = ref->Path();
        const std::size_t  slash = path.find_last_of('/');
        shown = slash == std::string::npos ? path : path.substr(slash + 1);
    }

    if (missing) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.40f, 1.0f));
    } else if (!ref->IsSet()) {
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    }

    // A BUTTON RATHER THAN A LABEL, even with no picker behind it, because a
    // button is a big obvious rectangle and this is a drop target. Dropping
    // onto a word is fiddly in a way nobody blames on the widget.
    const bool clear = ImGui::Button(shown.c_str(), ImVec2(ImGui::CalcItemWidth(), 0.0f));

    if (missing || !ref->IsSet()) {
        ImGui::PopStyleColor();
    }

    DrawFieldTooltip(script, field);

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kPayloadPrefab)) {
            // The payload is the virtual path as text, which is what every
            // asset payload in this editor carries - see AssetDragDrop.h.
            const std::string dropped(static_cast<const char*>(payload->Data),
                                      static_cast<std::size_t>(payload->DataSize));
            // Trimmed at the first NUL: the payload is sized to include the
            // terminator, and a std::string built from the raw bytes would
            // otherwise carry it and then fail to match any real path.
            ref->Set(dropped.c_str());
            changed = true;
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::SameLine();
    ImGui::TextUnformatted(field.name.c_str());

    if (clear && ref->IsSet()) {
        ref->Clear();
        changed = true;
    }

    if (missing) {
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "'%s' is not there",
                           ref->Path().c_str());
    } else if (!ref->IsSet()) {
        ImGui::TextDisabled("drag a prefab here from Assets");
    }

    return changed;
}

// ---------------------------------------------------------------------------
//  AN ANIMATION REFERENCE
//
//  IT ADDS NO WIDGET. DrawAssetSlot is the same slot a Sprite Animation
//  component's own clip gets and the same one a `// [animation]` string has
//  had all along - the marker was never the problem, being the only way to
//  ask for it was. So this is a TYPE in front of a widget that existed.
//
//  WHAT IS NEW IS THE RED. DrawStringField passes /*broken=*/false
//  unconditionally, so a clip somebody renamed in the slicer looked exactly
//  like one that was fine. Reporting the breakage while you are building the
//  level is the whole argument PrefabRef.h makes for having a type at all, so
//  a type that did not do it would be a rename and nothing more.
//
//  THE TYPE ITSELF IS NOT ASKED. An AnimationRef does not look - a reference
//  to a clip that does not exist yet is an ordinary state for a half-built
//  level. Looking is the Inspector's job, exactly as DrawPrefabRefField asks
//  FileSystem::Exists rather than making PrefabRef answer it.
//
//  NO DrawFieldTooltip CALL: the row draws one for every type but Entity, and
//  the slot sets its own against the button. Two here would fight.
// ---------------------------------------------------------------------------
bool DrawAnimationRefField(const eng::ScriptField& field) {
    eng::AnimationRef* ref = field.AsAnimationRef();
    if (ref == nullptr) {
        return false;
    }

    const bool broken = ref->IsSet() && !eng::SheetHasAnimation(ref->Path());

    std::string chosen;
    if (DrawAssetSlot(field.name.c_str(), AssetSlotKind::Animation, ref->Path(),
                      broken,
                      "drag an animation here from Assets - expand a sheet's row "
                      "to find them - or click to choose",
                      chosen)) {
        ref->Set(chosen);
        return true;
    }

    // UNDERNEATH AND IN WORDS, not only as a colour. "Red" does not say
    // whether the sheet moved or the clip was renamed, and those have
    // different fixes. The two cases read differently on purpose.
    if (broken) {
        const std::string sheet = ref->Sheet();
        if (sheet.empty()) {
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f),
                               "'%s' does not say which sheet it is in",
                               ref->Path().c_str());
        } else {
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f),
                               "%s has no animation called '%s'", sheet.c_str(),
                               ref->Clip().c_str());
        }
    }

    return false;
}

// ---------------------------------------------------------------------------
//  AN IMAGE REFERENCE
//
//  IT ADDS NO WIDGET EITHER. DrawAssetSlot is the same slot a Sprite
//  component's texture gets and the same one a `// [image]` string has always
//  had; this is a TYPE in front of a widget that existed, exactly as
//  DrawAnimationRefField is.
//
//  THE RED IS WHAT IT ADDS, and getting it right needs eng::ImageExists
//  rather than FileSystem::Exists - the picker offers sliced frames, and a
//  frame's path is not a file. See the note in DrawStringField, which had
//  that wrong for both kinds.
//
//  AND IT NAMES WHICH OF THE TWO THINGS IS WRONG, because they have different
//  fixes: a missing file is a move or a delete to undo, and a missing part is
//  a sheet to re-slice. "Red" says neither. The file is asked about FIRST, so
//  that a deleted sheet does not report itself as a sheet with no such frame.
//
//  NO DrawFieldTooltip CALL: the row draws one for every type but Entity, and
//  the slot sets its own against the button. Two here would fight.
// ---------------------------------------------------------------------------
bool DrawImageRefField(const eng::ScriptField& field) {
    eng::ImageRef* ref = field.AsImageRef();
    if (ref == nullptr) {
        return false;
    }

    const bool broken = ref->IsSet() && !eng::ImageExists(ref->Path());

    std::string chosen;
    if (DrawAssetSlot(field.name.c_str(), AssetSlotKind::Image, ref->Path(), broken,
                      "drag an image here from Assets, or click to choose one",
                      chosen)) {
        ref->Set(chosen);
        return true;
    }

    if (broken) {
        const std::string file = ref->File();
        if (!eng::FileSystem::Exists(file)) {
            // THE FILE ITSELF IS GONE, whichever part was named. Asked first
            // for that reason: "items.png has no part called 'key'" would be
            // true and useless when items.png is what was deleted.
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "'%s' is not there",
                               file.c_str());
        } else {
            // The file is there and the part is not, so it was re-sliced or
            // the frame was renamed. Frame() cannot be empty here: a whole
            // picture whose file exists is not broken.
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f),
                               "%s has no part called '%s'", file.c_str(),
                               ref->Frame().c_str());
        }
    }

    return false;
}

// One variable, drawn with whichever widget suits its type. True on a frame
// the value was actually changed.
//
// The component is here only for the entity reference, which needs to know
// whether the value is overridden in order to draw its own tooltip. Every
// other type ignores it.
bool DrawFieldWidget(const eng::ScriptComponent& script,
                     const eng::ScriptField& field) {
    const char* label = field.name.c_str();

    switch (field.type) {
        case eng::ScriptFieldType::Float: {
            float* value = field.AsFloat();
            if (value == nullptr) {
                break;
            }
            if (field.ranged) {
                return ImGui::SliderFloat(label, value, field.min, field.max);
            }
            return ImGui::DragFloat(label, value, DragStepFor(*value));
        }

        case eng::ScriptFieldType::Int: {
            int* value = field.AsInt();
            if (value == nullptr) {
                break;
            }
            if (field.ranged) {
                return ImGui::SliderInt(label, value, static_cast<int>(field.min),
                                        static_cast<int>(field.max));
            }
            // The same step the sprite's draw order uses, because a whole
            // number wants one pixel of mouse per unit and not less.
            return ImGui::DragInt(label, value, 0.2f);
        }

        case eng::ScriptFieldType::Bool: {
            bool* value = field.AsBool();
            if (value == nullptr) {
                break;
            }
            return ImGui::Checkbox(label, value);
        }

        case eng::ScriptFieldType::String:
            return DrawStringField(field);

        case eng::ScriptFieldType::Entity:
            return DrawEntityRefField(script, field);

        case eng::ScriptFieldType::Prefab:
            return DrawPrefabRefField(script, field);

        case eng::ScriptFieldType::Animation:
            return DrawAnimationRefField(field);

        case eng::ScriptFieldType::Image:
            return DrawImageRefField(field);

        case eng::ScriptFieldType::Vec2: {
            eng::Vec2* value = field.AsVec2();
            if (value == nullptr) {
                break;
            }
            float xy[2] = {value->x, value->y};
            // ONE step for both halves, taken from the larger. Two different
            // sensitivities on one widget would make a diagonal drag curve.
            const float step =
                DragStepFor(std::max(std::abs(xy[0]), std::abs(xy[1])));
            if (ImGui::DragFloat2(label, xy, step)) {
                *value = eng::Vec2{xy[0], xy[1]};
                return true;
            }
            return false;
        }

        case eng::ScriptFieldType::Color: {
            eng::Color* value = field.AsColor();
            if (value == nullptr) {
                break;
            }
            // ImGui works in 0-1 floats and the engine stores 0-255 bytes,
            // the same conversion DrawSprite does for a tint.
            float rgba[4] = {value->r / 255.0f, value->g / 255.0f, value->b / 255.0f,
                             value->a / 255.0f};
            if (ImGui::ColorEdit4(label, rgba)) {
                *value = eng::Color{static_cast<unsigned char>(rgba[0] * 255.0f),
                                    static_cast<unsigned char>(rgba[1] * 255.0f),
                                    static_cast<unsigned char>(rgba[2] * 255.0f),
                                    static_cast<unsigned char>(rgba[3] * 255.0f)};
                return true;
            }
            return false;
        }
    }

    // Reached only when a field's type and its pointer disagree, which the
    // AsFloat/AsInt/... accessors refuse to act on rather than casting
    // regardless. Saying so beats drawing a blank row.
    ImGui::TextDisabled("%s - cannot be shown (%s)", label,
                        eng::ScriptFieldTypeName(field.type));
    return false;
}

// What hovering a variable explains: the script's own note, the type, and
// whether this entity has its own value or is using the code's.
// ---------------------------------------------------------------------------
//  WHETHER IT IS OVERRIDDEN IS NOT A PARAMETER, and it used to be.
//
//  Three callers each worked the answer out and passed it in, which meant
//  three chances to pass a STALE one - and the field loop did exactly that,
//  handing over a flag from before a button that had since erased the value.
//  Asking the component costs a hash lookup and cannot be got wrong.
// ---------------------------------------------------------------------------
void DrawFieldTooltip(const eng::ScriptComponent& script,
                      const eng::ScriptField& field) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        return;
    }
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);

    if (!field.tooltip.empty()) {
        ImGui::TextUnformatted(field.tooltip.c_str());
        ImGui::Separator();
    }
    if (field.requiredType.empty()) {
        ImGui::TextDisabled("%s", eng::ScriptFieldTypeName(field.type));
    } else if (field.type == eng::ScriptFieldType::String) {
        // A MARKER ON A STRING NAMES A KIND OF ASSET, not a component type -
        // see ScriptFields::Asset. Without this the tooltip on an [animation]
        // variable read "an entity with a animation on it", which is nonsense
        // twice over.
        ImGui::TextDisabled("an asset path (%s)", field.requiredType.c_str());
    } else {
        // The constraint is part of what the field IS, so it belongs where the
        // type would otherwise be rather than as an extra line.
        ImGui::TextDisabled("an entity with a %s on it", field.requiredType.c_str());
    }

    // ======================================================================
    //  THE COMPONENT IS ASKED, not the flag that was passed in.
    //
    //  `overridden` is worked out once at the top of a field's row and this
    //  tooltip is drawn further down it, so it is a snapshot - and a button in
    //  between can erase the very value read here. That is not hypothetical:
    //  the revert button a few lines below did exactly that, and the at() in a
    //  sibling block THREW, which killed the editor outright.
    //
    //  This one happens to be safe today, because it runs before that button.
    //  Asking the authoritative question costs a hash lookup and does not
    //  depend on staying that way.
    // ======================================================================
    if (script.SavedValues().is_object() &&
        script.SavedValues().contains(field.name)) {
        ImGui::TextDisabled("saved on this entity as %s",
                            script.SavedValues().at(field.name).dump().c_str());
    } else {
        ImGui::TextDisabled("using the value from the script's own code - change it "
                            "here and it is saved on this entity only");
    }

    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

// True when it drew anything, so the caller can decide whether a separator
// belongs under it. Returned rather than asked for a second time: the answer
// costs a list of strings to work out, and working it out twice a frame for a
// question with one answer is the kind of waste that spreads.
bool DrawScriptFields(eng::ScriptComponent& script) {
    const eng::ScriptFields&       fields = script.Fields();
    const std::vector<std::string> unused = script.UnusedSavedValues();

    // A script that exposes nothing and has nothing saved gets no section at
    // all. Most scripts are in that position and an empty "Variables" header
    // on every one of them would be noise.
    if (fields.Empty() && unused.empty()) {
        return false;
    }

    ImGui::SeparatorText("Variables");

    for (const eng::ScriptField& field : fields.All()) {
        // Keyed on the NAME, so the widget keeps its identity when another
        // variable is added above it. An index would hand one field's
        // half-typed text to its neighbour.
        ImGui::PushID(field.name.c_str());

        const bool overridden = script.IsFieldOverridden(field.name);

        if (DrawFieldWidget(script, field)) {
            // The widget has already written the new value through the
            // field's own pointer; this records it as the saved one. Exactly
            // one piece of code - FieldToJson - knows how each type is
            // stored, and it is in the engine beside the code that reads it
            // back.
            script.SaveFieldValue(field.name);
            EditorState::Get().MarkDirty();
        }
        // Skipped for a reference, which drew its own against the button -
        // see the note on the forward declaration above.
        if (field.type != eng::ScriptFieldType::Entity) {
            DrawFieldTooltip(script, field);
        }

        // DIRECTLY AFTER THE WIDGET, so it reads as belonging to it. Anything
        // that has to be said about the value goes underneath, below.
        if (overridden) {
            ImGui::SameLine();
            if (ImGui::SmallButton("revert")) {
                script.RevertFieldValue(field.name);
                EditorState::Get().MarkDirty();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Forget this entity's value and go back to the one "
                                  "written in the script");
            }

            // ==============================================================
            //  ASKED AGAIN, AND THAT IS THE WHOLE FIX.
            //
            //  `overridden` was answered at the top of this iteration, BEFORE
            //  the revert button above - and pressing that button erases the
            //  very key the line below reads. So on the one frame a revert
            //  happened, this block was still entered and at() was called for
            //  a key that had just gone.
            //
            //  nlohmann's at() THROWS for a missing key, nothing here catches
            //  it, and an uncaught exception out of a draw call is not a
            //  message in the Console - it is the process dying with a crash
            //  dialogue. Reported as exactly that.
            //
            //  Nothing about this was particular to one kind of variable: any
            //  overridden field's revert button did it.
            // ==============================================================
            if (script.IsFieldOverridden(field.name)) {
                // ONE LOOKUP, held. Two calls to at() for the same key was
                // also how the first one came to be read without a guard.
                const eng::Json& saved = script.SavedValues().at(field.name);

                // A SAVED VALUE THAT WILL NOT LOAD. The engine reports this to
                // the Console when the scene opens, and then it is over - so
                // it is also shown here, next to the variable it is about, for
                // as long as it is true.
                if (!eng::JsonFitsFieldType(field.type, saved)) {
                    ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f),
                                       "the scene file has %s here, which is not a "
                                       "%s - the script's own value is being used",
                                       saved.dump().c_str(),
                                       eng::ScriptFieldTypeName(field.type));
                }
            }
        }

        // A RANGED VALUE OUTSIDE ITS RANGE, which is otherwise invisible: a
        // slider draws it pinned at whichever end it overshot, so 500 on a
        // 1-to-200 range looks exactly like 200. It can get there from a
        // hand-edited scene file or from the script setting it, because
        // nothing clamps on load - deliberately, since silently rewriting a
        // number somebody typed is worse than showing it.
        //
        // TOUCHING THE SLIDER WILL pull it into range, and that is worth
        // warning about before it happens rather than after.
        if (field.ranged) {
            const float value = field.type == eng::ScriptFieldType::Int
                                    ? static_cast<float>(*field.AsInt())
                                    : *field.AsFloat();
            if (value < field.min || value > field.max) {
                ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f),
                                   "%.3g is outside %.3g to %.3g - the slider shows it "
                                   "at the end, and moving it will bring it in",
                                   static_cast<double>(value),
                                   static_cast<double>(field.min),
                                   static_cast<double>(field.max));
            }
        }

        ImGui::PopID();
    }

    if (!fields.Empty()) {
        ImGui::TextDisabled("ctrl-click a number to type it exactly");
    }

    // ---- values nothing is using -----------------------------------------
    if (unused.empty()) {
        return true;
    }

    ImGui::Spacing();

    // THE TWO CAUSES ARE NOT THE SAME PROBLEM, so they do not get the same
    // message. An unresolved script claims nothing, so everything saved looks
    // unused and none of it is wrong - it is waiting. A running script with
    // leftovers has had a variable renamed or removed.
    if (script.IsResolved()) {
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f),
                           "%zu saved value(s) this script has no variable for",
                           unused.size());
        ImGui::TextWrapped("Renamed or removed since they were saved. They are kept and "
                           "still written to the scene file, so nothing is lost if the "
                           "variable comes back under its old name.");
    } else {
        ImGui::TextDisabled("%zu saved value(s), kept until the script compiles",
                            unused.size());
    }

    if (ImGui::TreeNode("Show them")) {
        for (const std::string& key : unused) {
            ImGui::BulletText("%s = %s", key.c_str(),
                              script.SavedValues().at(key).dump().c_str());
        }
        ImGui::TreePop();
    }

    // NOT OFFERED WHILE THE SCRIPT IS UNRESOLVED, where "unused" means all of
    // them and this button would be a one-click way to delete every setting
    // the component was keeping safe. ForgetUnusedSavedValues refuses in that
    // state as well; this is the half somebody can see.
    if (script.IsResolved() && ImGui::Button("Forget them")) {
        const std::size_t gone = script.ForgetUnusedSavedValues();
        EditorState::Get().MarkDirty();
        ENGINE_LOG_INFO(eng::Channels::kEditor,
                        "dropped {} saved value(s) that the script '{}' no longer has "
                        "variables for", gone, script.ScriptName());
    }
    return true;
}

void DrawScript(eng::ScriptComponent& script) {
    if (script.IsResolved()) {
        DrawScriptNameLink(script, ImVec4(0.55f, 0.85f, 0.60f, 1.0f));

        // WHICH HOOKS THIS SCRIPT ACTUALLY HAS.
        //
        // Hooks are found by name while the script compiles, so a misspelled
        // OnUpdate is not an error - it is a function nobody calls. Showing
        // the list here turns "why is nothing happening?" into something you
        // can answer by looking at the entity you are already looking at.
        const std::string hooks = eng::DescribeHooks(script.Hooks());
        ImGui::TextDisabled("compiled in and running");
        ImGui::TextWrapped("Hooks: %s", hooks.c_str());
    } else {
        // RED AND LOUD. A script that silently does nothing because it was
        // never compiled is the worst failure this feature can have, so it is
        // the most prominent thing in the panel.
        //
        // STILL A LINK, and if anything more useful as one: "this is not
        // running" is the moment you most want to open the file and find out
        // why. Not being compiled in does not mean the file is missing.
        DrawScriptNameLink(script, ImVec4(0.95f, 0.45f, 0.40f, 1.0f));
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), " -  NOT FOUND");
        // AND WHICH OF THE TWO REASONS IT IS. "Rebuild and restart the
        // editor" is actively wrong advice while a build is running, which
        // is precisely when every script in the scene reads as NOT FOUND -
        // the build unloads them for its duration. Telling somebody to
        // restart the editor to fix something that is fixing itself is how
        // this gets misdiagnosed as broken scripting.
        if (ScriptBuild::IsBuilding()) {
            ImGui::TextWrapped("The scripts are being compiled right now, which "
                               "unloads them while it runs. This will attach itself "
                               "when the build finishes - nothing needs doing.");
        } else {
            ImGui::TextWrapped("No script with that name is compiled into this "
                               "build, so it is attached and will be saved, but it "
                               "does not run. Rebuild the project - the connection "
                               "is by name, so nothing needs reattaching.");
        }
    }

    // THE VARIABLES, above the attach controls below. Tuning a script is what
    // somebody does repeatedly; choosing which script it is happens once.
    if (DrawScriptFields(script)) {
        ImGui::SeparatorText("Script");
    }

    // A list of what IS available, so a typo is one click from fixed rather
    // than a hunt through the scripts folder.
    if (ImGui::BeginCombo("Bind to", script.ScriptName().c_str())) {
        if (eng::ScriptRegistry::Count() == 0) {
            ImGui::TextDisabled("no scripts are compiled into this build");
        }
        eng::ScriptRegistry::ForEachScript([&](const char* name) {
            if (ImGui::Selectable(name, script.ScriptName() == name)) {
                script.SetScriptName(name);
                EditorState::Get().MarkDirty();
            }
        });
        ImGui::EndCombo();
    }

    ImGui::TextDisabled("or drag a .cpp from the Assets panel onto this window");
}

// ---------------------------------------------------------------------------
//  PREFABS
//
//  Two things live here: the BLOCK at the top of an instance - which prefab it
//  is, what has been changed about it, and the four things you can do - and the
//  per-component MARKS that say which parts of this entity you changed.
//
//  ==========================================================================
//  WHY THE MARKS ARE NAMED FIELDS AND NOT A BLUE BAR DOWN THE LEFT EDGE,
//  which is what Unity draws.
//
//  Every field in this panel is drawn by hand, by a function per component
//  type - see the top of this file. A bar beside exactly the right row would
//  mean all eighty-odd of those widgets knowing the JSON key they are saved
//  under, which is eighty chances to write the wrong one and a new one to get
//  wrong every time somebody adds a field.
//
//  The field NAMES come out of the diff for nothing. And they are the names in
//  the file, which this project already relies on elsewhere: a script's
//  variables are shown under their code names precisely so that what is on
//  screen and what is in the scene file are the same word. See the note above
//  DrawScriptFields.
//
//  ==========================================================================
//  ADDED AND REMOVED COMPONENTS ARE HANDLED IN THE BLOCK AT THE TOP, not
//  beside the component. Matching a live component to an "added" record means
//  deciding which of two same-type components is which, and the honest answer
//  is a positional one that would be wrong in exactly the confusing cases. The
//  Overrides list names them and offers Apply and Revert for each, which is
//  the whole capability; only the placement of the button differs.
// ---------------------------------------------------------------------------

// The same blue the Hierarchy colours an instance's rows with. It means the
// same thing in both places: this is one copy of something.
const ImVec4 kPrefabBlue{0.55f, 0.78f, 0.95f, 1.0f};

// Records what the panel wants done. NOT DONE HERE - applying or reverting
// rebuilds every entity in the instance, and this panel is in the middle of
// describing one of them. See PrefabRequest in EditorApp.h.
void RequestPrefab(PrefabAction action, eng::EntityId member,
                   const eng::PrefabOverrideItem& item = {}) {
    PrefabRequest& request = EditorState::Get().prefabRequest;
    request.action         = action;
    request.member         = member;
    request.item           = item;
}

// One row of the override list: what it is, and the two ways out of it.
void DrawOverrideRow(const eng::PrefabOverrideItem& item, eng::EntityId member,
                     const char* label) {
    ImGui::PushID(item.description.c_str());

    ImGui::TextColored(kPrefabBlue, "%s", label);
    ImGui::SameLine();

    if (ImGui::SmallButton("Revert")) {
        RequestPrefab(PrefabAction::RevertItem, member, item);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Put this back to what the prefab says.");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Apply")) {
        RequestPrefab(PrefabAction::ApplyItem, member, item);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Write this one change into the prefab, so every other "
                          "instance gets it too.\n\nThis edits a FILE and cannot be "
                          "undone with Ctrl+Z.");
    }

    ImGui::PopID();
}

// The block at the top. Returns the override list so the component loop below
// can mark itself up without running the diff a second time - it walks every
// component of every member of the instance, which is not free.
std::vector<eng::PrefabOverrideItem> DrawPrefabBlock(eng::Entity& entity) {
    eng::Scene&          scene    = eng::Engine::Get().GetScene();
    eng::PrefabInstance* instance = scene.PrefabInstanceOf(entity.Id());
    if (instance == nullptr) {
        return {};
    }

    ImGui::PushStyleColor(ImGuiCol_Text, kPrefabBlue);
    ImGui::SeparatorText("Prefab");
    ImGui::PopStyleColor();

    std::string             assetError;
    const eng::PrefabAsset* asset =
        eng::PrefabLibrary::Load(instance->prefabPath, assetError);

    // ---- the prefab cannot be read ---------------------------------------
    //
    // SAID IN FULL, because the two ways out are to put the file back or to
    // unpack, and neither is guessable from "something went wrong".
    if (asset == nullptr) {
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f),
                           "this prefab cannot be read");
        ImGui::TextWrapped("%s", assetError.c_str());
        ImGui::TextDisabled("The changes made to this instance are kept and still "
                            "saved, so putting the file back restores the link.");
        if (ImGui::Button("Unpack Prefab")) {
            RequestPrefab(PrefabAction::Unpack, entity.Id());
        }
        ImGui::Separator();
        return {};
    }

    ImGui::TextColored(kPrefabBlue, "%s", instance->prefabPath.c_str());
    if (!scene.IsPrefabRoot(entity.Id())) {
        ImGui::TextDisabled("part of an instance - the buttons below act on all of it");
    }

    // THE DIFF, once per frame while an instance is selected. It serialises
    // every component of every member, so it is not free - which is why the
    // result is handed back rather than asked for again per component.
    std::vector<eng::PrefabOverrideItem> overrides =
        eng::ListPrefabOverrides(scene, *asset, *instance);

    if (ImGui::SmallButton("Select Asset")) {
        EditorState::Get().revealAsset = instance->prefabPath;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Show the prefab file in the Assets panel.");
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(overrides.empty());
    if (ImGui::SmallButton("Apply All")) {
        RequestPrefab(PrefabAction::ApplyAll, entity.Id());
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Write every change below into the prefab, so every other "
                          "instance of it gets them.\n\nThis edits a FILE and cannot "
                          "be undone with Ctrl+Z.");
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("Revert All")) {
        RequestPrefab(PrefabAction::RevertAll, entity.Id());
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Throw every change below away, so this instance matches the "
                          "prefab again.\n\nWhere it stands in the scene is kept.");
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::SmallButton("Unpack")) {
        RequestPrefab(PrefabAction::Unpack, entity.Id());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Break the link. The entities stay exactly as they are and "
                          "become ordinary ones.");
    }

    // ---- what has been changed about this instance ------------------------
    if (overrides.empty()) {
        ImGui::TextDisabled("nothing changed - this matches the prefab");
    } else {
        char label[64];
        std::snprintf(label, sizeof(label), "Changed here (%zu)###prefab_overrides",
                      overrides.size());
        if (ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen)) {
            // THE WHOLE INSTANCE, not just the selected entity, which is what
            // Unity's Overrides dropdown shows and the only useful answer:
            // "what have I changed about this enemy" is a question about the
            // enemy rather than about whichever part of it is selected.
            for (const eng::PrefabOverrideItem& item : overrides) {
                DrawOverrideRow(item, entity.Id(), item.description.c_str());
            }
            ImGui::TreePop();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Everything about this instance that differs from the "
                              "prefab.\n\nWhere it stands in the scene is not listed: "
                              "every instance is somewhere different, so its position "
                              "and rotation are not changes to the prefab.");
        }
    }

    ImGui::Separator();
    return overrides;
}

// Does this exact component carry any overridden field?
bool ComponentIsOverridden(const std::vector<eng::PrefabOverrideItem>& overrides,
                           int localId, std::string_view type, int slot) {
    if (localId == 0) {
        return false;
    }
    for (const eng::PrefabOverrideItem& item : overrides) {
        if (item.kind != eng::PrefabOverrideKind::Property) {
            continue;
        }
        if (item.property.localId == localId && item.property.component == type &&
            item.property.slot == slot) {
            return true;
        }
    }
    return false;
}

// The fields of this component that differ from the prefab, each with its own
// way out. Drawn inside the component, under its own heading.
void DrawComponentOverrides(const std::vector<eng::PrefabOverrideItem>& overrides,
                            eng::EntityId member, int localId, std::string_view type,
                            int slot) {
    if (localId == 0) {
        return;
    }
    bool any = false;
    for (const eng::PrefabOverrideItem& item : overrides) {
        if (item.kind != eng::PrefabOverrideKind::Property) {
            continue;
        }
        if (item.property.localId != localId || item.property.component != type ||
            item.property.slot != slot) {
            continue;
        }
        if (!any) {
            ImGui::Separator();
            ImGui::TextColored(kPrefabBlue, "changed from the prefab:");
            any = true;
        }
        DrawOverrideRow(item, member, item.property.field.c_str());
    }
}

} // namespace

void InspectorPanel::ApplyPendingRemoval() {
    if (!m_pendingRemoval.requested) {
        return;
    }

    // Taken and CLEARED FIRST, so an early return below cannot leave a
    // request that tries again on every frame for the rest of the session.
    const PendingRemoval request = m_pendingRemoval;
    m_pendingRemoval             = {};

    eng::Entity* entity = eng::Engine::Get().GetScene().Get(request.entity);
    if (entity == nullptr) {
        return;   // destroyed between the click and here; the id did its job
    }

    // CHECKED AGAIN, not trusted. The index was true when the x was clicked,
    // and anything that ran in between could have changed the list - so the
    // component at that position is confirmed to still be the kind that may
    // be removed before anything is erased.
    eng::Component* component = entity->ComponentAt(request.index);
    if (component == nullptr) {
        return;
    }
    const std::string typeName = component->TypeName();
    if (typeName == eng::TransformComponent::kTypeName) {
        ENGINE_LOG_ERROR(eng::Channels::kEditor,
                         "refused to remove the transform from '{}' - every entity "
                         "has one", entity->Name());
        return;
    }

    if (entity->RemoveComponentAt(request.index)) {
        EditorState::Get().MarkDirty();
        ENGINE_LOG_INFO(eng::Channels::kEditor, "removed {} from '{}'", typeName,
                        entity->Name());
    }
}

// ---------------------------------------------------------------------------
//  THE SELECTED ASSET, rather than the selected entity.
//
//  Click an image in the Assets panel and this is what the Inspector shows:
//  how that file is imported. Same panel, same gesture Unity uses, and
//  deliberately NOT a second window - "the thing I just clicked, described" is
//  one idea, and an entity and a file are both things you click.
//
//  ==========================================================================
//  WHY THESE SETTINGS BELONG TO THE FILE AND NOT TO THE SPRITE
//
//  Because "is this pixel art?" has one answer per picture, and the
//  alternative is answering it again on every entity that uses the picture. A
//  character used by forty entities would be forty copies of one fact,
//  thirty-nine of which are waiting to disagree with the fortieth.
//
//  The sprite's own settings - its tint, its draw order, its size - are
//  per-entity and stay on the component, where the same argument puts them.
//
//  ==========================================================================
//  A CHANGE TAKES EFFECT BEFORE YOU LET GO OF THE MOUSE.
//
//  Choosing a scaling mode writes the .oimport AND re-applies it to the
//  already-loaded texture, so every sprite in the open scene changes at once
//  and the preview here changes with them. No Apply button and no reimport
//  step, because there is nothing to recompute: the filter is a property of
//  the graphics object rather than of the pixels.
//
//  That matters more than it sounds. "Is this the right scaling for this
//  sprite?" is a question about how it LOOKS, and the only useful way to
//  answer it is to look at it while changing it.
//
//  ==========================================================================
//  NOTHING HERE CALLS MarkDirty, AND THAT IS NOT AN OVERSIGHT.
//
//  These settings are not part of the scene. They are a file on disk, written
//  the moment they change. Marking the scene would put an asterisk in the
//  title bar for an edit that saving the scene does not store, and push an
//  undo snapshot that Ctrl+Z cannot undo - see the note on MarkDirty in
//  EditorApp.h for why undo listens to that flag.
// ---------------------------------------------------------------------------
namespace {

// The preview is drawn this many pixels on its longest side.
//
// DELIBERATELY NOT 1:1. At its native size a texture is sampled exactly and
// the scaling mode makes no visible difference at all - which would leave this
// panel's one setting apparently doing nothing on the very screen that offers
// it. Most sprites in a 2D game are small, so a fixed box is an ENLARGEMENT
// for them, and an enlargement is where Nearest and Linear differ most.
constexpr float kPreviewPixels = 128.0f;

void DrawTexturePreview(const eng::TextureRef& texture) {
    // ======================================================================
    //  IT ALWAYS OCCUPIES THE SLOT, even when there is nothing to show.
    //
    //  Both callers follow it with SameLine, which attaches to the LAST item
    //  submitted - so returning without submitting anything put the details
    //  column beside whatever happened to be above the preview instead,
    //  which for the frame Inspector is the separator. A Dummy of the same
    //  size keeps the layout honest for an image that failed to load.
    // ======================================================================
    const float wide = texture ? static_cast<float>(texture->width) : 0.0f;
    const float high = texture ? static_cast<float>(texture->height) : 0.0f;
    if (!texture || texture->native == nullptr || wide <= 0.0f ||
        high <= 0.0f) {
        ImGui::Dummy(ImVec2(kPreviewPixels, kPreviewPixels));
        return;
    }

    const float scale = kPreviewPixels / ((wide > high) ? wide : high);
    ImGui::Image(reinterpret_cast<ImTextureID>(texture->native),
                 ImVec2(wide * scale, high * scale));
}

// The two buttons and the combo, shared by the texture and the font sections
// because the question and the words for it are the same. Returns true when
// the value changed.
bool DrawFilterChoice(eng::TextureFilter& filter) {
    bool changed = false;

    // ---- the presets ------------------------------------------------------
    //
    // TWO BUTTONS FOR THE TWO ANSWERS PEOPLE ACTUALLY HAVE. Somebody who has
    // just drawn a 16x16 character knows they drew pixel art; they do not
    // necessarily know that "Nearest" is the word for what they want. The
    // combo below is the precise control, and these are the same thing in the
    // vocabulary of the person choosing.
    ImGui::TextDisabled("what kind of picture is this?");
    if (ImGui::Button("Pixel Art")) {
        filter  = eng::TextureFilter::Nearest;
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Crisp, square pixels when enlarged.\n"
                          "For art drawn pixel by pixel.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Smooth / UI")) {
        filter  = eng::TextureFilter::Linear;
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Blended, soft edges when scaled.\n"
                          "For interface art, photographs and gradients.");
    }

    ImGui::Spacing();

    // ---- and the setting itself -------------------------------------------
    const char* names[] = {
        eng::TextureFilterName(eng::TextureFilter::Nearest),
        eng::TextureFilterName(eng::TextureFilter::Linear),
    };
    int current = (filter == eng::TextureFilter::Linear) ? 1 : 0;
    if (ImGui::Combo("Scaling", &current, names, 2)) {
        filter = (current == 1) ? eng::TextureFilter::Linear
                                : eng::TextureFilter::Nearest;
        changed = true;
    }

    ImGui::TextDisabled("%s", filter == eng::TextureFilter::Nearest
                                  ? "nearest: doubles pixels up - pixel art"
                                  : "linear: blends pixels - smooth art");
    return changed;
}

// Where the settings are kept, and the way back to the defaults. Shown for
// every kind that HAS settings, so the file is never a mystery.
void DrawSidecarFooter(const std::string& path) {
    ImGui::Separator();

    if (eng::ImportLibrary::HasSettings(path)) {
        ImGui::TextDisabled("stored in");
        ImGui::TextDisabled("%s", eng::ImportLibrary::SidecarPath(path).c_str());

        if (ImGui::Button("Revert to defaults")) {
            std::string error;
            if (eng::ImportLibrary::Revert(path, error)) {
                eng::ResourceManager::RefreshImportSettings(path);
            } else {
                ENGINE_LOG_ERROR(eng::Channels::kEditor,
                                 "could not revert the import settings for '{}': {}",
                                 path, error);
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Deletes the settings file beside this asset.");
        }
    } else {
        // SAYING SO MATTERS. The absence of a file IS the defaults - see
        // ImportLibrary::Save - so without this line somebody who had set
        // something, then set it back, would watch the file disappear and
        // reasonably conclude the editor had lost their settings.
        ImGui::TextDisabled("no settings file - this asset uses the defaults,");
        ImGui::TextDisabled("and one is written as soon as you change something");
    }
}

// The image the Inspector is previewing, and which file it came from.
//
// ONE OBJECT RATHER THAN TWO LOOSE STATICS so that there is something to
// clear: see ForgetHeldTexture below, which exists because a static holding
// a GPU texture outlives the GPU.
struct HeldPreview {
    std::string     path;
    eng::TextureRef texture;
};

HeldPreview& Held() {
    static HeldPreview held;
    return held;
}

const eng::TextureRef& HeldTexture(const std::string& path) {
    // ======================================================================
    //  HELD ACROSS FRAMES, AND IT HAS TO BE.
    //
    //  This function runs every frame a file is selected. A TextureRef that
    //  lived only for the call would often be the LAST owner of the image, so
    //  the texture would be destroyed at the end of every frame and the file
    //  read, decoded and uploaded again on the next one - sixty times a
    //  second, for as long as somebody leaves an image selected.
    //
    //  The Assets panel's thumbnail usually owns it too and hides that
    //  entirely. The case where it does not is the one that matters: navigate
    //  to another folder and Refresh throws the thumbnails away, while the
    //  selection deliberately survives - see the note where that clear was
    //  removed in AssetBrowserPanel::Draw. That is exactly when somebody is
    //  sitting looking at this panel.
    //
    //  A function-local static rather than a member of InspectorPanel, which
    //  is the same trick EditorApp::ReloadChangedPrefabs uses to keep
    //  <filesystem> out of a header every panel includes.
    //
    //  ONE IMAGE STAYS LOADED after you select something else, until the next
    //  image is selected. That is the same single-image cost the panel would
    //  pay anyway while it was on screen.
    // ======================================================================
    //  SHARED BY THE SHEET AND THE FRAME INSPECTORS, which is why it is a
    //  function of its own rather than three statics inside one panel: only
    //  one of them draws in any given frame, so one held image between them
    //  is the whole cost, and switching from a sheet to one of its frames
    //  loads the frame and lets the sheet go.
    // ======================================================================
    HeldPreview& held = Held();
    if (held.path != path) {
        held.path    = path;
        held.texture = eng::ResourceManager::LoadTexture(path);
    }
    return held.texture;
}

// ---------------------------------------------------------------------------
//  AND IT HAS TO BE LET GO BEFORE SDL IS.
//
//  A function-local static is destroyed when the PROGRAM ends - after the
//  renderer has been shut down and SDL has been quit. Texture's destructor
//  calls SDL_DestroyTexture, and SDL frees a renderer's textures along with
//  the renderer, so that call would be made on memory already given back: an
//  exit that crashes occasionally, which is the worst kind of crash to own.
//
//  It also left ResourceManager::Shutdown reporting the image as "still in use
//  at shutdown", which is the line that found this.
//
//  InspectorPanel's destructor is where this is called from, and that works
//  for a stated reason: EditorApp::Shutdown clears the panels BEFORE
//  Engine::Shutdown, precisely so that a panel's destructor can still reach
//  engine state. See the note on m_panels.clear().
// ---------------------------------------------------------------------------
void ForgetHeldTexture() {
    Held() = HeldPreview{};
}

// ---------------------------------------------------------------------------
//  The slicer
//
//  A spritesheet is one file holding many pictures, and the engine already
//  knows how to hand out one of them: "textures/hero.png#walk_1" loads the
//  rectangle called walk_1 out of hero.png - see ImportSettings.h. What it
//  cannot do on its own is work out where those rectangles are. That is what
//  this section is for, and the whole of it writes nothing but the four grid
//  numbers and a list of rectangles into the same .oimport sidecar.
//
//  ==========================================================================
//  TWO KINDS OF STATE, AND KEEPING THEM APART IS THE DESIGN.
//
//  The GRID NUMBERS are a DRAFT. They are what the next press of Slice would
//  cut, they are dragged rather than typed, and they are not written anywhere
//  until that press. A draggable number has to be held across frames: this
//  panel re-reads the sidecar every frame, so a slider writing straight back
//  into `settings` would be read back as its old value on the next frame and
//  appear frozen - and one that wrote to the FILE every frame would hit the
//  disk sixty times a second for the length of a drag.
//
//  EVERYTHING ELSE is the committed slice, read fresh from ImportLibrary every
//  frame and written the moment it changes. Those widgets are deliberately the
//  discrete kind - type and press Enter, click a button, tick a box - so that
//  one gesture is one write and the "snaps back while you drag it" problem
//  cannot arise.
//
//  ==========================================================================
//  ANYTHING THAT ROUND-TRIPS IS REFUSED BY Save, NOT BY THIS FILE.
//
//  A name that is not a name, two frames called the same thing, an animation
//  with nothing in it: every one of those is refused by ImportLibrary::Save,
//  because that is where the rules belong - see IsLegalSheet. So an edit here
//  is made OPTIMISTICALLY into a local copy and offered to Save, and a refusal
//  is shown to the person in Save's own words. The edit then simply does not
//  stick, because the next frame re-reads the file that was not written. A
//  second copy of those rules in the editor would be a second copy to drift.
//
//  ==========================================================================
//  THREE THINGS Save CANNOT SEE, AND THEY ARE REFUSED HERE.
//
//  Each one round-trips through the file perfectly, so IsLegalSheet - which is
//  handed a SheetSlice and nothing else - has no way to object, and each is
//  refused at the point of the gesture instead, saying why:
//
//    * A RECTANGLE OUTSIDE THE IMAGE. Save never learns the image's size; this
//      panel has the texture. See the rectangle editor in the Frames list.
//    * A FRAME RATE OF ZERO OR LESS. Clamped as it is typed, because 0 fps
//      round-trips and is nobody's intention.
//    * AN ANIMATION EMPTIED ONE FRAME AT A TIME. The reader drops an animation
//      with no frames, so removing the last one is refused and the whole
//      animation offered for deletion instead.
//
//  Anything added to that list belongs here, with the same shape: say what is
//  wrong, at the moment it is asked for, and do not apply it.
// ---------------------------------------------------------------------------

// The slicer's own preview, bigger than the one above it.
//
// A SEPARATE CONSTANT RATHER THAN A BIGGER kPreviewPixels. That one is sized
// to make the scaling mode visible on a small sprite; this one has a grid
// drawn over it and has to be big enough to count the cells in.
constexpr float kSlicePreviewPixels = 288.0f;

// Past this many cells along one side, the grid is not drawn at all.
//
// A CEILING ON THE DRAWING, NOT ON THE SLICING. 288 pixels divided into 300
// cells is not a grid, it is a grey rectangle - and it would cost six hundred
// lines a frame to say nothing. The outline is still drawn and the count is
// still written out underneath.
constexpr int kMaxGridLines = 64;

// And the same ceiling for outlining frames that have already been cut, which
// are drawn one rectangle each because they need not line up with any grid.
constexpr int kMaxOutlinedFrames = 512;

// ---------------------------------------------------------------------------
//  The draft - see the note above on why the grid numbers are held
// ---------------------------------------------------------------------------
struct SliceDraft {
    std::string     path;       // the image these numbers belong to
    eng::SheetSlice lastSeen;   // the committed slice they were last seeded from

    int cellWidth  = 0;
    int cellHeight = 0;
    int margin     = 0;
    int spacing    = 0;

    // The same grid said the other way round. Some sheets are described by
    // their cell size ("16 by 16") and some by their layout ("4 across, 3
    // down"), and which one somebody knows depends on where the sheet came
    // from. Both produce the same four numbers in the file.
    bool byCount = false;
    int  columns = 1;
    int  rows    = 1;

    char prefix[32]       = "f";
    char newAnimation[48] = "";
    int  pickedFrame      = 0;   // the index the Append combo is showing

    // The last thing the slicer said - a refusal from Save, or a note about a
    // guess. Kept across frames because it is the answer to a click that has
    // already finished happening.
    std::string note;
    bool        noteIsError = false;

    bool askUnmark = false;   // the confirmation popup wants opening
};

// ---------------------------------------------------------------------------
//  The one draft, and the rule for when it is re-seeded.
//
//  ONE, not one per image, for the same reason the preview holds one texture:
//  only one asset is selected at a time, and the draft is a scratch pad rather
//  than a record of anything.
//
//  RE-SEEDED WHENEVER THE COMMITTED SLICE CHANGES UNDERNEATH IT, which is one
//  rule covering three situations that would otherwise each need their own:
//  this panel just wrote a slice, Revert to defaults threw one away, and
//  somebody edited the .oimport in a text editor and came back - see
//  EditorApp's focus-regain, which re-reads them all.
//
//  The grid numbers survive a re-seed when the file has nothing to say about
//  them, so unticking the spritesheet box and ticking it again does not throw
//  away what had been typed.
// ---------------------------------------------------------------------------
SliceDraft& DraftFor(const std::string& path, const eng::SheetSlice& committed) {
    static SliceDraft draft;

    bool seed = false;
    if (draft.path != path) {
        draft      = SliceDraft{};
        draft.path = path;
        seed       = true;
    } else if (draft.lastSeen != committed) {
        seed = true;
    }

    if (seed) {
        draft.lastSeen = committed;
        if (committed.cellWidth > 0) {
            draft.cellWidth = committed.cellWidth;
        }
        if (committed.cellHeight > 0) {
            draft.cellHeight = committed.cellHeight;
        }
        draft.margin  = committed.margin;
        draft.spacing = committed.spacing;
    }
    return draft;
}

// The four numbers the draft currently describes, whichever way round they
// were entered. In "by how many" mode the cell size is DERIVED, which is where
// a sheet that does not divide evenly shows up as a leftover.
eng::SheetSlice GridOf(const SliceDraft& draft, int imageWidth, int imageHeight) {
    eng::SheetSlice grid;
    grid.margin  = draft.margin;
    grid.spacing = draft.spacing;

    if (!draft.byCount) {
        grid.cellWidth  = draft.cellWidth;
        grid.cellHeight = draft.cellHeight;
        return grid;
    }

    if (draft.columns > 0) {
        const int usable =
            imageWidth - 2 * draft.margin - (draft.columns - 1) * draft.spacing;
        grid.cellWidth = (usable > 0) ? usable / draft.columns : 0;
    }
    if (draft.rows > 0) {
        const int usable =
            imageHeight - 2 * draft.margin - (draft.rows - 1) * draft.spacing;
        grid.cellHeight = (usable > 0) ? usable / draft.rows : 0;
    }
    return grid;
}

// ---------------------------------------------------------------------------
//  The ONE place the sheet section writes anything.
//
//  Every edit in this section goes through here, so that the refusal message
//  from Save reaches the screen whatever the edit was, and so that there is
//  exactly one place that remembers to forget the frames already cut.
// ---------------------------------------------------------------------------
bool CommitSheet(const std::string& path, const eng::TextureImport& settings,
                 SliceDraft& draft) {
    std::string error;
    if (!eng::ImportLibrary::Save(path, settings, error)) {
        draft.note        = error;
        draft.noteIsError = true;
        ENGINE_LOG_ERROR(eng::Channels::kEditor, "could not slice '{}': {}", path,
                         error);
        return false;
    }

    // THE FRAMES ALREADY CUT ARE NOW WRONG. A frame texture is cached under
    // "<sheet>#<name>", and re-slicing moves the rectangle that name refers
    // to - so without this the cache would go on handing out the old pixels
    // for a name whose meaning has changed.
    eng::ResourceManager::InvalidateSubAssets(path);

    // AND THE ASSETS PANEL HAS TO HEAR ABOUT IT. Its listing is cached and
    // refreshed on demand - deliberately, see the note in its header - so
    // slicing a sheet would otherwise leave this panel showing twelve frames
    // while the tile over there grew no disclosure arrow until somebody
    // pressed Refresh. Two panels disagreeing about one file is the bug the
    // note on m_picked was written to prevent.
    EditorState::Get().assetsNeedRefresh = true;

    draft.note.clear();
    draft.noteIsError = false;
    return true;
}

void DrawDraftNote(const SliceDraft& draft) {
    if (draft.note.empty()) {
        return;
    }
    if (draft.noteIsError) {
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s",
                           draft.note.c_str());
    } else {
        ImGui::TextDisabled("%s", draft.note.c_str());
    }
}

// ---------------------------------------------------------------------------
//  The big preview, with the grid over it
//
//  Returns the name of a frame that was clicked, or an empty string. Clicking
//  one selects it, which is how the frame Inspector is reached before the
//  Assets panel learns to list frames of its own.
// ---------------------------------------------------------------------------
std::string DrawSlicePreview(const eng::TextureRef& texture,
                             const eng::SheetSlice& committed,
                             const eng::SheetSlice& grid,
                             const eng::SliceGeometry& geometry) {
    std::string clicked;

    if (!texture || texture->native == nullptr || texture->width <= 0 ||
        texture->height <= 0) {
        ImGui::TextDisabled("the image has to load before it can be sliced");
        return clicked;
    }

    const float wide = static_cast<float>(texture->width);
    const float high = static_cast<float>(texture->height);

    // Shrunk to fit a narrow panel rather than sticking out of it. Everything
    // below is in terms of `scale`, so the overlay follows automatically.
    float box = kSlicePreviewPixels;
    if (const float room = ImGui::GetContentRegionAvail().x;
        room > 64.0f && room < box) {
        box = room;
    }
    const float scale = box / ((wide > high) ? wide : high);

    ImGui::Image(reinterpret_cast<ImTextureID>(texture->native),
                 ImVec2(wide * scale, high * scale));

    const ImVec2 origin  = ImGui::GetItemRectMin();
    const ImVec2 corner  = ImGui::GetItemRectMax();
    const bool   hovered = ImGui::IsItemHovered();
    ImDrawList*  draw    = ImGui::GetWindowDrawList();

    // ======================================================================
    //  FIVE ARGUMENTS, AND THAT IS NOT A TIDINESS CHOICE.
    //
    //  ImGui 1.92.8 swapped the last two parameters of AddRect: the current
    //  signature is (min, max, colour, rounding, THICKNESS, flags). The
    //  six-argument calls elsewhere in this editor were written against the
    //  old order and still work, because ImGui keeps an inline overload in the
    //  old order that forwards to the new one - but that overload is compiled
    //  out by IMGUI_DISABLE_OBSOLETE_FUNCTIONS and is deleted outright in its
    //  place. New code calls the real signature.
    // ======================================================================
    draw->AddRect(origin, corner, IM_COL32(255, 255, 255, 70), 0.0f, 1.0f);

    const auto toScreenX = [&](int imagePixel) {
        return origin.x + static_cast<float>(imagePixel) * scale;
    };
    const auto toScreenY = [&](int imagePixel) {
        return origin.y + static_cast<float>(imagePixel) * scale;
    };

    // ---- what is already cut ---------------------------------------------
    //
    // Drawn one rectangle each rather than as a grid, because a frame that has
    // been nudged or renamed by hand has no grid that describes it any more -
    // which is exactly why the frames are stored and not recomputed.
    const bool tooMany =
        committed.frames.size() > static_cast<std::size_t>(kMaxOutlinedFrames);
    const bool outlineFrames = committed.IsSliced() && !tooMany;
    if (outlineFrames) {
        for (const eng::SheetFrame& frame : committed.frames) {
            draw->AddRect(ImVec2(toScreenX(frame.x), toScreenY(frame.y)),
                          ImVec2(toScreenX(frame.x + frame.w),
                                 toScreenY(frame.y + frame.h)),
                          IM_COL32(120, 190, 250, 150), 0.0f, 1.0f);
        }
    }

    // ---- and what the next press of Slice would cut ----------------------
    //
    // Only when it would come out DIFFERENT, so that the usual case - a sheet
    // sliced by the grid still shown - is one set of lines and not two on top
    // of each other.
    // Compared number by number rather than with the struct comparison,
    // because `committed` carries the frames as well and `grid` never does -
    // so the two would always differ and both sets of lines would always draw.
    const bool sameGrid = grid.cellWidth == committed.cellWidth &&
                          grid.cellHeight == committed.cellHeight &&
                          grid.margin == committed.margin &&
                          grid.spacing == committed.spacing;

    const bool showGrid = geometry.count > 0 && (!outlineFrames || !sameGrid);
    if (showGrid) {
        if (geometry.columns > kMaxGridLines || geometry.rows > kMaxGridLines) {
            ImGui::TextDisabled("the grid is too fine to draw - %d by %d cells",
                                geometry.columns, geometry.rows);
        } else {
            const ImU32 colour = IM_COL32(250, 220, 120, 170);
            const int   stepX  = grid.cellWidth + grid.spacing;
            const int   stepY  = grid.cellHeight + grid.spacing;

            // BOTH EDGES OF EVERY CELL, not one line per boundary, because
            // with a spacing the cells do not touch and a single line would
            // put the gap inside a frame.
            for (int column = 0; column < geometry.columns; ++column) {
                const int left = grid.margin + column * stepX;
                draw->AddLineV(toScreenX(left), origin.y, corner.y, colour, 1.0f);
                draw->AddLineV(toScreenX(left + grid.cellWidth), origin.y, corner.y,
                               colour, 1.0f);
            }
            for (int row = 0; row < geometry.rows; ++row) {
                const int top = grid.margin + row * stepY;
                draw->AddLineH(origin.x, corner.x, toScreenY(top), colour, 1.0f);
                draw->AddLineH(origin.x, corner.x, toScreenY(top + grid.cellHeight),
                               colour, 1.0f);
            }
        }
    }

    // ---- the frame under the cursor ---------------------------------------
    if (hovered && outlineFrames) {
        const ImVec2 mouse = ImGui::GetMousePos();
        const int    x = static_cast<int>((mouse.x - origin.x) / scale);
        const int    y = static_cast<int>((mouse.y - origin.y) / scale);

        // Tested against the RECTANGLES rather than worked out from the grid,
        // so that hand-moved frames and overlapping ones still answer
        // correctly. Backwards, so the frame drawn last is the one picked.
        for (std::size_t i = committed.frames.size(); i > 0; --i) {
            const eng::SheetFrame& frame = committed.frames[i - 1];
            if (x < frame.x || y < frame.y || x >= frame.x + frame.w ||
                y >= frame.y + frame.h) {
                continue;
            }
            draw->AddRect(ImVec2(toScreenX(frame.x), toScreenY(frame.y)),
                          ImVec2(toScreenX(frame.x + frame.w),
                                 toScreenY(frame.y + frame.h)),
                          IM_COL32(255, 255, 255, 255), 0.0f, 2.0f);
            ImGui::SetTooltip("%s\n%d, %d  %d x %d\nclick to look at it",
                              frame.name.c_str(), frame.x, frame.y, frame.w, frame.h);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                clicked = frame.name;
            }
            break;
        }
    }

    // SAID, RATHER THAN JUST NOT DRAWN. A sliced sheet with no outlines over
    // it looks exactly like one that was never sliced.
    if (tooMany) {
        ImGui::TextDisabled("%zu frames is too many to outline here",
                            committed.frames.size());
    }

    return clicked;
}

// ---------------------------------------------------------------------------
//  The grid numbers. Writes to the DRAFT only - nothing here touches a file.
// ---------------------------------------------------------------------------
void DrawGridControls(SliceDraft& draft, const eng::TextureRef& texture,
                      const eng::SliceGeometry& geometry) {
    if (ImGui::RadioButton("by cell size", !draft.byCount)) {
        draft.byCount = false;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("by how many", draft.byCount)) {
        // Seeded from whatever the cell-size mode was showing, so switching
        // over does not land on 1 by 1 and lose the place.
        draft.byCount = true;
        if (geometry.columns > 0) {
            draft.columns = geometry.columns;
            draft.rows    = geometry.rows;
        }
    }

    if (draft.byCount) {
        int counts[2] = {draft.columns, draft.rows};
        if (ImGui::DragInt2("Across, down", counts, 0.2f, 1, 1024)) {
            draft.columns = counts[0];
            draft.rows    = counts[1];
        }
        ImGui::TextDisabled("the cell size is worked out from these");
    } else {
        int cell[2] = {draft.cellWidth, draft.cellHeight};
        if (ImGui::DragInt2("Cell size", cell, 0.5f, 1, 4096)) {
            draft.cellWidth  = cell[0];
            draft.cellHeight = cell[1];
        }
    }

    int padding[2] = {draft.margin, draft.spacing};
    if (ImGui::DragInt2("Margin, spacing", padding, 0.2f, 0, 1024)) {
        draft.margin  = padding[0];
        draft.spacing = padding[1];
    }
    ImGui::TextDisabled("margin: the border skipped on all four sides");
    ImGui::TextDisabled("spacing: the gap between neighbouring cells");

    // ---- the guess --------------------------------------------------------
    if (ImGui::Button("Guess from the image size")) {
        int guess = 0;
        if (texture && eng::GuessSheetCell(texture->width, texture->height, guess)) {
            draft.byCount    = false;
            draft.cellWidth  = guess;
            draft.cellHeight = guess;
            draft.margin     = 0;
            draft.spacing    = 0;
            draft.note       = "guessed " + std::to_string(guess) + " by " +
                         std::to_string(guess) + " from the dimensions alone - "
                         "check the preview";
            draft.noteIsError = false;
        } else {
            draft.note = "nothing in this image's dimensions suggests a cell "
                         "size; set one yourself";
            draft.noteIsError = true;
        }
    }
    if (ImGui::IsItemHovered()) {
        // SAID WHERE SOMEBODY IS ABOUT TO PRESS IT. "Auto-detect" in other
        // tools looks at the pixels; this cannot, and a guess that silently
        // pretended otherwise would be worse than no button.
        ImGui::SetTooltip("Works from the width and height ONLY.\n"
                          "The editor has no way to look at the pixels of a\n"
                          "loaded image, so this cannot find the sprites in\n"
                          "a sheet with uneven padding. Check the preview.");
    }
}

// ---------------------------------------------------------------------------
//  Frames
// ---------------------------------------------------------------------------
void DrawFrameList(const std::string& path, eng::TextureImport& settings,
                   SliceDraft& draft, int imageWidth, int imageHeight) {
    eng::SheetSlice& sheet = settings.sheet;

    // OPEN BY DEFAULT. This section only exists for an image somebody has
    // marked as a spritesheet, and for that image the frames ARE the content -
    // a header you have to open to find out whether slicing worked is a header
    // in the way.
    if (!ImGui::CollapsingHeader("Frames", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    if (sheet.frames.empty()) {
        ImGui::TextDisabled("none yet - set a grid above and press Slice");
        return;
    }

    ImGui::TextDisabled("%zu frame(s). Each one loads as its own image:",
                        sheet.frames.size());
    ImGui::TextDisabled("%s",
                        eng::MakeSubAssetPath(path, sheet.frames.front().name).c_str());

    bool changed = false;
    int  remove  = -1;

    // A BOX WITH ITS OWN SCROLLBAR, because a sheet of a hundred frames would
    // otherwise push everything below it - the animations, the settings file,
    // the way back to the defaults - off the bottom of the panel.
    ImGui::BeginChild("##frames", ImVec2(0.0f, 200.0f), ImGuiChildFlags_Borders);
    for (std::size_t i = 0; i < sheet.frames.size(); ++i) {
        eng::SheetFrame& frame = sheet.frames[i];

        ImGui::PushID(static_cast<int>(i));
        if (ImGui::TreeNode("##frame", "%s      %d, %d   %d x %d", frame.name.c_str(),
                            frame.x, frame.y, frame.w, frame.h)) {
            char name[64];
            std::snprintf(name, sizeof(name), "%s", frame.name.c_str());
            if (ImGui::InputText("Name", name, sizeof(name),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                // ======================================================
                //  THE ANIMATIONS HAVE TO FOLLOW, and this is the only
                //  place that can do it.
                //
                //  An animation holds frame NAMES rather than rectangles,
                //  deliberately - see SheetAnimation - so that moving a
                //  rectangle does not have to be chased through everything
                //  using it. The cost is exactly this: a rename that did
                //  not update them would leave every animation pointing at
                //  a name that no longer exists, and a missing name is
                //  SKIPPED when an animation is resolved. The animation
                //  would quietly shorten rather than break.
                // ======================================================
                const std::string was = frame.name;
                for (eng::SheetAnimation& animation : sheet.animations) {
                    for (std::string& member : animation.frames) {
                        if (member == was) {
                            member = name;
                        }
                    }
                }
                frame.name = name;
                changed    = true;
            }
            ImGui::TextDisabled("press Enter to rename");

            int rect[4] = {frame.x, frame.y, frame.w, frame.h};
            if (ImGui::InputInt4("x, y, w, h", rect,
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                // ==========================================================
                //  REFUSED HERE, BECAUSE HERE IS WHERE THE IMAGE'S SIZE IS.
                //
                //  Save cannot do it: IsLegalSheet is handed a SheetSlice
                //  with no width or height in it, so "9000" in the w box
                //  round-trips through the file perfectly and comes back as a
                //  frame reaching four thousand pixels past the right edge.
                //  The engine then refuses to cut it, once, in the log - and
                //  the sprite that asked for it draws the magenta placeholder
                //  while this list goes on showing the number as if it were
                //  fine.
                //
                //  Two keystrokes is all it takes, so this is not a defensive
                //  check against a hand-edited file. It is the only thing
                //  standing between a typo and a frame that cannot load.
                //
                //  THE WHOLE RECTANGLE IS REFUSED, not clamped. Clamping w
                //  from 9000 to 32 would silently answer a different question
                //  from the one that was asked.
                // ==========================================================
                const bool fits = rect[2] > 0 && rect[3] > 0 && rect[0] >= 0 &&
                                  rect[1] >= 0 && imageWidth > 0 &&
                                  imageHeight > 0 && rect[2] <= imageWidth &&
                                  rect[3] <= imageHeight &&
                                  rect[0] <= imageWidth - rect[2] &&
                                  rect[1] <= imageHeight - rect[3];
                if (fits) {
                    frame.x = rect[0];
                    frame.y = rect[1];
                    frame.w = rect[2];
                    frame.h = rect[3];
                    changed = true;
                } else {
                    draft.note = "that rectangle is not inside the image, which "
                                 "is " + std::to_string(imageWidth) + " by " +
                                 std::to_string(imageHeight) + " pixels";
                    draft.noteIsError = true;
                }
            }

            if (ImGui::SmallButton("Look at it")) {
                EditorState::Get().selectedAsset = eng::MakeSubAssetPath(path,
                                                                         frame.name);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) {
                remove = static_cast<int>(i);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    if (remove >= 0) {
        // THE ANIMATIONS ARE LEFT ALONE ON PURPOSE. A removed frame's name
        // stops resolving and is skipped, which is recoverable by putting the
        // frame back; silently editing it out of every animation is not.
        sheet.frames.erase(sheet.frames.begin() + remove);
        changed = true;
    }

    if (ImGui::Button("Remove every frame")) {
        sheet.frames.clear();
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Anything using a path like\n"
                          "\"%s\"\nwill stop finding its picture.",
                          eng::MakeSubAssetPath(path, "walk_1").c_str());
    }

    if (changed) {
        CommitSheet(path, settings, draft);
    }
}

// ---------------------------------------------------------------------------
//  Animations
//
//  STORED IN THE SAME PLACE AND ADDRESSED THE SAME WAY as a frame, which is
//  the whole point: "textures/hero.png#walk" is a path like any other, and
//  anything that can take an image can take it.
// ---------------------------------------------------------------------------
void DrawAnimationList(const std::string& path, eng::TextureImport& settings,
                       SliceDraft& draft, const eng::SliceGeometry& geometry) {
    eng::SheetSlice& sheet = settings.sheet;

    if (!ImGui::CollapsingHeader("Animations", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    bool changed = false;
    int  remove  = -1;

    if (sheet.animations.empty()) {
        ImGui::TextDisabled("none yet");
    } else {
        ImGui::BeginChild("##animations", ImVec2(0.0f, 220.0f),
                          ImGuiChildFlags_Borders);
        for (std::size_t i = 0; i < sheet.animations.size(); ++i) {
            eng::SheetAnimation& animation = sheet.animations[i];

            ImGui::PushID(static_cast<int>(i));
            if (ImGui::TreeNode("##animation", "%s      %zu frame(s), %.1f fps%s",
                                animation.name.c_str(), animation.frames.size(),
                                static_cast<double>(animation.fps),
                                animation.loop ? ", looping" : ", once")) {
                char name[64];
                std::snprintf(name, sizeof(name), "%s", animation.name.c_str());
                if (ImGui::InputText("Name", name, sizeof(name),
                                     ImGuiInputTextFlags_EnterReturnsTrue)) {
                    animation.name = name;
                    changed        = true;
                }

                float fps = animation.fps;
                if (ImGui::InputFloat("Frames a second", &fps, 0.0f, 0.0f, "%.1f",
                                      ImGuiInputTextFlags_EnterReturnsTrue)) {
                    // CLAMPED HERE RATHER THAN REFUSED BY Save. A rate of zero
                    // round-trips through the file perfectly well, so it is not
                    // IsLegalSheet's business - but it is nobody's intention.
                    animation.fps = (fps < 0.1f) ? 0.1f : fps;
                    changed       = true;
                }

                bool loop = animation.loop;
                if (ImGui::Checkbox("Repeat for ever", &loop)) {
                    animation.loop = loop;
                    changed        = true;
                }

                bool resume = animation.resume;
                if (ImGui::Checkbox("Carry on where it left off", &resume)) {
                    animation.resume = resume;
                    changed          = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "When something plays THIS animation while it is already\n"
                        "the one loaded, carry on from the frame it stopped on\n"
                        "rather than starting over.\n\n"
                        "On for a walk cycle: stopping and walking off again\n"
                        "should not visibly hitch. Off for a swing or a jump,\n"
                        "or the second one begins half way through the first.\n\n"
                        "A clip that ran to its end always starts again - there\n"
                        "is nothing to carry on with.");
                }

                ImGui::Spacing();
                int moveFrom = -1;
                int moveTo   = -1;
                int dropped  = -1;
                for (std::size_t f = 0; f < animation.frames.size(); ++f) {
                    const std::string& member = animation.frames[f];
                    ImGui::PushID(static_cast<int>(f));

                    ImGui::Text("%zu.", f + 1);
                    ImGui::SameLine();
                    if (sheet.FindFrame(member) != nullptr) {
                        ImGui::Text("%s", member.c_str());
                    } else {
                        // SAID IN RED AND NOT REFUSED. The reader keeps a name
                        // that matches no frame, and resolving the animation
                        // skips it - so this is an animation that is quietly
                        // shorter than it looks, which is worth seeing.
                        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f),
                                           "%s  - no such frame", member.c_str());
                    }

                    // DISABLED AT THE ENDS RATHER THAN IGNORED THERE. These used
                    // to be drawn live and have the press thrown away by a
                    // trailing condition, so the first row's "up" highlighted,
                    // depressed, returned true and did nothing at all - a button
                    // that works and achieves nothing, repeatably.
                    ImGui::SameLine();
                    ImGui::BeginDisabled(f == 0);
                    if (ImGui::SmallButton("up")) {
                        moveFrom = static_cast<int>(f);
                        moveTo   = static_cast<int>(f) - 1;
                    }
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    ImGui::BeginDisabled(f + 1 >= animation.frames.size());
                    if (ImGui::SmallButton("down")) {
                        moveFrom = static_cast<int>(f);
                        moveTo   = static_cast<int>(f) + 1;
                    }
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (ImGui::SmallButton("x")) {
                        dropped = static_cast<int>(f);
                    }
                    ImGui::PopID();
                }
                if (moveFrom >= 0) {
                    std::swap(animation.frames[static_cast<std::size_t>(moveFrom)],
                              animation.frames[static_cast<std::size_t>(moveTo)]);
                    changed = true;
                }
                if (dropped >= 0) {
                    if (animation.frames.size() == 1) {
                        // REFUSED HERE, with the reason, rather than offered and
                        // then refused by Save. An animation with nothing in it
                        // is dropped by the reader, so the alternative is a
                        // confusing error on a perfectly reasonable-looking
                        // click. Deleting the whole animation is right below.
                        draft.note = "an animation needs at least one frame - "
                                     "delete the animation instead";
                        draft.noteIsError = true;
                    } else {
                        animation.frames.erase(animation.frames.begin() + dropped);
                        changed = true;
                    }
                }

                // ---- appending a frame ------------------------------------
                if (!sheet.frames.empty()) {
                    // Rebuilt every frame the section is open, which is a few
                    // thousand pointers at the very worst and only while
                    // somebody is looking at this list.
                    std::vector<const char*> names;
                    names.reserve(sheet.frames.size());
                    for (const eng::SheetFrame& frame : sheet.frames) {
                        names.push_back(frame.name.c_str());
                    }
                    if (draft.pickedFrame >= static_cast<int>(names.size())) {
                        draft.pickedFrame = 0;
                    }

                    ImGui::SetNextItemWidth(160.0f);
                    ImGui::Combo("##pick", &draft.pickedFrame, names.data(),
                                 static_cast<int>(names.size()));
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Append")) {
                        animation.frames.push_back(
                            sheet.frames[static_cast<std::size_t>(draft.pickedFrame)]
                                .name);
                        changed = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Append all")) {
                        for (const eng::SheetFrame& frame : sheet.frames) {
                            animation.frames.push_back(frame.name);
                        }
                        changed = true;
                    }
                }

                if (ImGui::SmallButton("Delete this animation")) {
                    remove = static_cast<int>(i);
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    if (remove >= 0) {
        sheet.animations.erase(sheet.animations.begin() + remove);
        changed = true;
    }

    // ---- making one -------------------------------------------------------
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputTextWithHint("##newanimation", "walk", draft.newAnimation,
                             sizeof(draft.newAnimation));
    ImGui::SameLine();
    ImGui::BeginDisabled(sheet.frames.empty() || draft.newAnimation[0] == '\0');
    if (ImGui::Button("Add animation")) {
        // STARTED WITH EVERY FRAME IN IT, and not empty, because an empty one
        // cannot be saved: the reader drops an animation that lists no frames,
        // so writing one would show it until the next reload and then lose it.
        // Every frame is also the right guess for a sheet that is one strip.
        eng::SheetAnimation made;
        made.name = draft.newAnimation;
        for (const eng::SheetFrame& frame : sheet.frames) {
            made.frames.push_back(frame.name);
        }
        sheet.animations.push_back(std::move(made));
        changed = true;
        draft.newAnimation[0] = '\0';
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("a new animation starts with every frame in it;");
    ImGui::TextDisabled("take out the ones it does not use");

    // ---- the common layout, in one press ----------------------------------
    //
    // OFFERED ONLY WHEN THE FRAMES STILL MATCH THE GRID, because that is the
    // only time "a row" means anything: once a frame has been moved or removed
    // by hand, the nth group of `columns` frames is not a row of the picture.
    const bool matchesGrid =
        geometry.count > 0 && geometry.columns > 0 &&
        static_cast<long long>(sheet.frames.size()) == geometry.count;
    if (matchesGrid) {
        if (ImGui::Button("One animation per row")) {
            int made    = 0;
            int skipped = 0;
            for (int row = 0; row < geometry.rows; ++row) {
                const std::string name = "row" + std::to_string(row);
                if (sheet.FindFrame(name) != nullptr ||
                    sheet.FindAnimation(name) != nullptr) {
                    ++skipped;
                    continue;
                }
                eng::SheetAnimation animation;
                animation.name = name;
                for (int column = 0; column < geometry.columns; ++column) {
                    const std::size_t index =
                        static_cast<std::size_t>(row) *
                            static_cast<std::size_t>(geometry.columns) +
                        static_cast<std::size_t>(column);
                    animation.frames.push_back(sheet.frames[index].name);
                }
                sheet.animations.push_back(std::move(animation));
                ++made;
            }
            if (made > 0) {
                changed = true;
            }
            draft.note = std::to_string(made) + " animation(s) added" +
                         (skipped > 0 ? ", " + std::to_string(skipped) +
                                            " skipped - the name was taken"
                                      : "");
            draft.noteIsError = (made == 0);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Most sheets put one movement on each row.\n"
                              "Makes row0, row1, ... from the %d rows.",
                              geometry.rows);
        }
    }

    if (changed) {
        CommitSheet(path, settings, draft);
    }
}

// ---------------------------------------------------------------------------
//  The whole spritesheet section
// ---------------------------------------------------------------------------
void DrawSheetSection(const std::string& path, eng::TextureImport& settings,
                      const eng::TextureRef& texture) {
    SliceDraft& draft = DraftFor(path, settings.sheet);

    // ======================================================================
    //  "MARKED AS A SPRITESHEET" IS NOT A FLAG IN THE FILE.
    //
    //  It is simply whether the sheet block says anything at all - a grid, or
    //  frames, or both. A separate bool would be a second source of truth
    //  about the same question, free to disagree with the frames beside it,
    //  and it would have to be read, written, defaulted and migrated.
    //
    //  So ticking the box writes a grid (guessed, or the whole image when
    //  there is nothing to guess from) and unticking it clears the block. The
    //  box therefore cannot be ticked without a usable grid underneath it,
    //  which is the one thing that would make it appear not to stick.
    // ======================================================================
    bool marked = !(settings.sheet == eng::SheetSlice{});
    if (ImGui::Checkbox("This image is a spritesheet", &marked)) {
        if (!marked) {
            if (settings.sheet.frames.empty() && settings.sheet.animations.empty()) {
                settings.sheet = eng::SheetSlice{};
                CommitSheet(path, settings, draft);
                return;
            } else {
                draft.askUnmark = true;
            }
        } else {
            int guess = 0;
            if (texture && eng::GuessSheetCell(texture->width, texture->height,
                                               guess)) {
                settings.sheet.cellWidth  = guess;
                settings.sheet.cellHeight = guess;
            } else if (texture && texture->width > 0 && texture->height > 0) {
                settings.sheet.cellWidth  = texture->width;
                settings.sheet.cellHeight = texture->height;
                draft.note = "no cell size to guess from the dimensions - "
                             "set one below";
                draft.noteIsError = false;
            } else {
                draft.note        = "the image has to load before it can be sliced";
                draft.noteIsError = true;
                return;
            }
            CommitSheet(path, settings, draft);
            return;
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("A spritesheet is one file holding many pictures.\n"
                          "Slicing it gives each one a name and makes it\n"
                          "usable anywhere an image is - including tiles.");
    }

    // ---- giving it up is destructive, so it is asked about ----------------
    if (draft.askUnmark) {
        ImGui::OpenPopup("Not a spritesheet?");
        draft.askUnmark = false;
    }
    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Not a spritesheet?", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("This throws away %zu frame(s) and %zu animation(s).",
                    settings.sheet.frames.size(), settings.sheet.animations.size());
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f),
                           "Anything already using a path like");
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f), "   %s",
                           eng::MakeSubAssetPath(path, "walk_1").c_str());
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f),
                           "will stop finding its picture, and this editor");
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f),
                           "cannot tell you what that is.");

        bool cleared = false;
        if (ImGui::Button("Throw them away", ImVec2(150, 0))) {
            settings.sheet = eng::SheetSlice{};
            CommitSheet(path, settings, draft);
            cleared = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep them", ImVec2(150, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();

        if (cleared) {
            return;
        }
    }

    if (!marked) {
        return;
    }

    // ---- the slicer -------------------------------------------------------
    ImGui::Separator();

    // ======================================================================
    //  A PLACEHOLDER HAS NO SIZE, as far as this panel is concerned.
    //
    //  An image that will not decode - a truncated download, a git-lfs
    //  pointer, a .psd renamed to .png - comes back as the shared 32x32
    //  magenta square with isPlaceholder set. Taking the size off THAT drew
    //  a slicing grid over the magenta, offered to re-slice a 512x512 sheet
    //  into 32x32 frames, and refused a frame's own existing rectangle with
    //  "that rectangle is not inside the image, which is 32 by 32 pixels" -
    //  a sentence that is simply false about the file on screen.
    //
    //  Zero instead, which every consumer below already treats as "not yet":
    //  MeasureGrid refuses it with "the image has no size", the preview says
    //  the image has to load first, and the rectangle editor refuses an edit
    //  rather than inventing a bound for it.
    // ======================================================================
    const bool measurable = texture && !texture->isPlaceholder;
    const int  imageWidth  = measurable ? texture->width : 0;
    const int  imageHeight = measurable ? texture->height : 0;

    const eng::SheetSlice    grid = GridOf(draft, imageWidth, imageHeight);
    const eng::SliceGeometry geometry =
        eng::MeasureGrid(imageWidth, imageHeight, grid);

    const std::string picked =
        DrawSlicePreview(texture, settings.sheet, grid, geometry);
    if (!picked.empty()) {
        EditorState::Get().selectedAsset = eng::MakeSubAssetPath(path, picked);
    }

    DrawGridControls(draft, texture, geometry);

    // ---- what that comes to ----------------------------------------------
    if (geometry.count > 0) {
        ImGui::Text("%d across, %d down - %lld frame(s) of %d x %d",
                    geometry.columns, geometry.rows, geometry.count, grid.cellWidth,
                    grid.cellHeight);
        if (geometry.leftoverX > 0 || geometry.leftoverY > 0) {
            ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f),
                               "%d pixel(s) over on the right, %d along the bottom",
                               geometry.leftoverX, geometry.leftoverY);
            ImGui::TextDisabled("blank space in the sheet is fine; otherwise one");
            ImGui::TextDisabled("of the four numbers above is not quite right");
        }
    } else {
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s",
                           geometry.reason.c_str());
    }

    // ---- and the button that writes it down -------------------------------
    ImGui::SetNextItemWidth(120.0f);
    ImGui::InputText("Name frames", draft.prefix, sizeof(draft.prefix));
    ImGui::SameLine();
    ImGui::TextDisabled("%s0, %s1, ...", draft.prefix, draft.prefix);

    const bool alreadySliced = settings.sheet.IsSliced();
    if (alreadySliced) {
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f),
                           "slicing again replaces all %zu frame(s);",
                           settings.sheet.frames.size());
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f),
                           "names you have changed will not survive it");
    }

    ImGui::BeginDisabled(geometry.count <= 0);
    if (ImGui::Button(alreadySliced ? "Slice again" : "Slice")) {
        std::string                        reason;
        const std::vector<eng::SheetFrame> cut =
            eng::SliceGrid(imageWidth, imageHeight, grid, draft.prefix, reason);
        if (cut.empty()) {
            draft.note        = reason;
            draft.noteIsError = true;
        } else {
            // THE GRID IS STORED WITH THE FRAMES so that coming back to this
            // panel shows what was last asked for rather than starting again.
            settings.sheet.cellWidth  = grid.cellWidth;
            settings.sheet.cellHeight = grid.cellHeight;
            settings.sheet.margin     = grid.margin;
            settings.sheet.spacing    = grid.spacing;
            settings.sheet.frames     = cut;

            // THE ANIMATIONS ARE KEPT. They hold frame NAMES, and a re-slice
            // with the same prefix produces the same names - so an animation
            // over f0 to f3 still means something afterwards. Throwing them
            // away would be the one irreversible part of a reversible action.
            CommitSheet(path, settings, draft);
        }
    }
    ImGui::EndDisabled();

    // THE LIMIT OF ALL OF THIS, SAID ONCE. InvalidateSubAssets cannot reach a
    // texture something is already holding - the cache is weak - so a sprite
    // in the open scene goes on drawing the old rectangle until it asks again.
    //
    // AN ANIMATION IS THE EXCEPTION NOW, and the difference is worth stating
    // because it used to be the worst case rather than the best. A
    // SpriteAnimationComponent holds the frames it is playing, so
    // InvalidateSubAssets cannot reach them either - but it remembers the
    // ImportLibrary generation it resolved at, and slicing moves that, so it
    // re-reads the animation and re-cuts every frame on its next step. See
    // SpriteAnimation.h.
    if (alreadySliced) {
        ImGui::TextDisabled("a sprite already showing one frame keeps it until");
        ImGui::TextDisabled("the scene is reloaded; a playing animation re-reads");
        ImGui::TextDisabled("itself and follows this straight away");
    }

    DrawDraftNote(draft);

    DrawFrameList(path, settings, draft, imageWidth, imageHeight);
    DrawAnimationList(path, settings, draft, geometry);
}

// ---------------------------------------------------------------------------
//  One frame of a sheet, selected on its own
//
//  READ-ONLY, AND THAT IS THE POINT. A frame is not a file: it has no settings
//  of its own, it cannot be renamed here, and ImportLibrary::Save refuses its
//  path outright - "import settings belong to the whole image, not to one
//  frame of it". Everything that CAN be changed about it is a property of the
//  sheet, so this panel's job is to show what it is and offer the way there.
// ---------------------------------------------------------------------------
void DrawSubAssetInspector(const std::string& path) {
    const std::string sheetPath = eng::SheetPathOf(path);
    const std::string name      = eng::SubAssetNameOf(path);

    const eng::TextureRef& texture = HeldTexture(path);
    DrawTexturePreview(texture);
    ImGui::SameLine();
    ImGui::BeginGroup();
    if (texture && !texture->isPlaceholder) {
        ImGui::Text("%d x %d pixels", texture->width, texture->height);
    } else {
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f),
                           "this frame could not be cut");
        ImGui::TextDisabled("the log says why - usually a rectangle");
        ImGui::TextDisabled("that reaches outside the image");
    }
    ImGui::TextDisabled("part of a spritesheet");
    ImGui::EndGroup();

    ImGui::Separator();

    // ======================================================================
    //  ForTexture IS ASKED ABOUT THE SHEET, NEVER ABOUT THIS PATH.
    //
    //  A sidecar's path is derived by APPENDING, so asking about a frame would
    //  look for "textures/hero.png#walk_1.oimport" - a file that does not and
    //  must not exist. Everything shown below belongs to the sheet.
    // ======================================================================
    const eng::TextureImport sheetSettings = eng::ImportLibrary::ForTexture(sheetPath);

    const eng::SheetFrame*     frame     = sheetSettings.sheet.FindFrame(name);
    const eng::SheetAnimation* animation = sheetSettings.sheet.FindAnimation(name);

    if (frame != nullptr) {
        ImGui::Text("Frame '%s'", name.c_str());
        ImGui::TextDisabled("at %d, %d, %d by %d pixels of the sheet", frame->x,
                            frame->y, frame->w, frame->h);
    } else if (animation != nullptr) {
        ImGui::Text("Animation '%s'", name.c_str());
        ImGui::TextDisabled("%zu frame(s) at %.1f fps, %s", animation->frames.size(),
                            static_cast<double>(animation->fps),
                            animation->loop ? "repeating" : "played once");

        // WHICH FRAME THIS PATH ACTUALLY DRAWS, said plainly. Loading an
        // animation's path gives its FIRST frame - a still - because a path
        // names a picture and an animation is not one.
        if (!animation->frames.empty()) {
            ImGui::TextDisabled("as an image this is its first frame, '%s'",
                                animation->frames.front().c_str());
        }

        // AND WHAT MAKES IT MOVE, which is the question the line above
        // immediately raises. Dropping this onto an entity does both halves -
        // the still on the sprite and a component to play it - so the answer
        // is one gesture rather than an instruction to go and add something.
        ImGui::TextDisabled("drag it onto an entity to play it: that adds a");
        ImGui::TextDisabled("Sprite Animation beside the sprite");
        for (const std::string& member : animation->frames) {
            if (sheetSettings.sheet.FindFrame(member) == nullptr) {
                ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f),
                                   "'%s' is not a frame of this sheet",
                                   member.c_str());
            }
        }
    } else {
        // The sheet was re-sliced, or the name was changed, while something
        // was still pointing here.
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f),
                           "the sheet has nothing called '%s'", name.c_str());
    }

    ImGui::Separator();
    ImGui::TextDisabled("scaling and slicing belong to the whole sheet:");
    ImGui::TextDisabled("%s", sheetPath.c_str());
    if (ImGui::Button("Open the sheet")) {
        EditorState::Get().selectedAsset = sheetPath;
    }
}

void DrawTextureImport(const std::string& path) {
    const eng::TextureRef& texture = HeldTexture(path);

    DrawTexturePreview(texture);
    ImGui::SameLine();
    ImGui::BeginGroup();
    if (texture) {
        ImGui::Text("%d x %d pixels", texture->width, texture->height);
        if (texture->isPlaceholder) {
            // THE SHARED MAGENTA SQUARE, not this image. Worth saying plainly,
            // because the preview above is showing a picture that is not the
            // file you selected.
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f),
                               "this image failed to load");
        } else {
            // WHAT SDL ACTUALLY HAS, not what the file below asks for. The two
            // agree whenever the editor made the change; this is how a sidecar
            // edited outside and not yet re-read shows up as the disagreement
            // it is, rather than as the panel confidently reporting its own
            // input back to itself.
            ImGui::TextDisabled("drawn with %s right now",
                                eng::Renderer::TextureIsSmooth(texture)
                                    ? eng::TextureFilterName(eng::TextureFilter::Linear)
                                    : eng::TextureFilterName(eng::TextureFilter::Nearest));
        }
    }
    ImGui::TextDisabled("the preview is enlarged, so that");
    ImGui::TextDisabled("the scaling mode is visible in it");
    ImGui::EndGroup();

    ImGui::Separator();

    eng::TextureImport settings = eng::ImportLibrary::ForTexture(path);
    if (DrawFilterChoice(settings.filter)) {
        std::string error;
        if (eng::ImportLibrary::Save(path, settings, error)) {
            // THE LIVE TEXTURE TOO, not just the file. Without this the
            // setting would be right on the next run and appear to do nothing
            // now, which is indistinguishable from broken.
            eng::ResourceManager::RefreshImportSettings(path);
        } else {
            ENGINE_LOG_ERROR(eng::Channels::kEditor,
                             "could not save the import settings for '{}': {}", path,
                             error);
        }
    }

    ImGui::Separator();

    // AFTER THE SCALING AND BEFORE THE FOOTER. Both sections write the same
    // sidecar, and the footer is what says where that file is - so it belongs
    // underneath everything that writes to it.
    //
    // It writes its own changes rather than reporting them back here, which is
    // why nothing is done with a return value: see CommitSheet.
    DrawSheetSection(path, settings, texture);

    DrawSidecarFooter(path);
}

void DrawFontImport(const std::string& path) {
    // NO PREVIEW. A font is baked into one atlas PER SIZE, so there is no
    // single image to show - and the thing you would want to look at is the
    // text in the Game view, which is already on screen.
    ImGui::TextDisabled("a font is baked into a separate image for each size");
    ImGui::TextDisabled("it is used at; this applies to all of them");
    ImGui::Separator();

    eng::FontImport settings = eng::ImportLibrary::ForFont(path);
    if (DrawFilterChoice(settings.filter)) {
        std::string error;
        if (eng::ImportLibrary::Save(path, settings, error)) {
            eng::ResourceManager::RefreshImportSettings(path);
        } else {
            ENGINE_LOG_ERROR(eng::Channels::kEditor,
                             "could not save the import settings for '{}': {}", path,
                             error);
        }
    }

    // SMOOTH IS THE DEFAULT HERE AND NEAREST IS THE DEFAULT FOR AN IMAGE, and
    // saying so where somebody can see it saves them wondering whether the
    // panel is confused. A letter is a curve approximated into coverage
    // values; drawing those unfiltered throws away what makes it look curved.
    ImGui::TextDisabled("fonts default to Linear, unlike images");

    DrawSidecarFooter(path);
}

void DrawAssetInspector(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    const std::string name  = (slash == std::string::npos) ? path
                                                           : path.substr(slash + 1);

    ImGui::Text("%s", name.c_str());
    ImGui::TextDisabled("%s", path.c_str());
    ImGui::Separator();

    // ======================================================================
    //  A FRAME OF A SHEET IS NOT A FILE, AND THIS TEST COMES FIRST.
    //
    //  Before the switch below, because ClassifyAsset deliberately answers
    //  Texture for "textures/hero.png#walk_1" - a frame of an image is an
    //  image, which is what makes one draggable onto an entity. Falling into
    //  DrawTextureImport with that path would ask ImportLibrary about
    //  "textures/hero.png#walk_1.oimport": a sidecar that does not and must
    //  not exist, since a sidecar's path is derived by APPENDING. Every
    //  setting shown would be a default, and every change would be refused.
    //
    //  So it is taken before ANY ImportLibrary call. See DrawSubAssetInspector
    //  for the one call it does make, which is about the sheet.
    // ======================================================================
    if (eng::IsSubAsset(path)) {
        DrawSubAssetInspector(path);
        return;
    }

    switch (ClassifyAsset(path)) {
        case AssetKind::Texture:
            DrawTextureImport(path);
            return;
        case AssetKind::Font:
            DrawFontImport(path);
            return;

        // ---- everything else -------------------------------------------
        //
        // SAID OUT LOUD RATHER THAN LEFT BLANK, and each one points at where
        // that kind of file IS edited. An empty Inspector after clicking a
        // file reads as a panel that has failed, and the question somebody
        // actually has is "then where do I change this?".
        case AssetKind::Scene:
            ImGui::TextDisabled("a scene has no import settings.");
            ImGui::TextDisabled("double-click it to open it.");
            return;
        case AssetKind::Prefab:
            ImGui::TextDisabled("a prefab has no import settings.");
            ImGui::TextDisabled("double-click it to place one, then edit that.");
            return;
        case AssetKind::Tile:
            ImGui::TextDisabled("a tile's settings are its own file.");
            ImGui::TextDisabled("double-click it to open the Tile Palette.");
            return;
        case AssetKind::Script:
            ImGui::TextDisabled("a script has no import settings.");
            ImGui::TextDisabled("its variables appear on the entity it is attached to.");
            return;
        case AssetKind::Unknown:
            break;
    }

    ImGui::TextDisabled("this kind of file has no import settings.");
}

} // namespace

// Nothing but letting the preview go. See ForgetHeldTexture.
InspectorPanel::~InspectorPanel() {
    ForgetHeldTexture();
}

void InspectorPanel::Draw() {
    // FIRST, before anything reads the component list - including the early
    // return below for "nothing is selected". Carrying it out at the end of
    // Draw instead would look the same almost always, and then quietly strand
    // the request on the one path that returns early: click the x, click away
    // before the next frame, and the removal would sit there unapplied.
    //
    // The cost is that the component stays on screen for the remainder of the
    // frame its x was clicked on, which at sixty frames a second nobody sees.
    ApplyPendingRemoval();

    EditorState& state = EditorState::Get();
    eng::Scene&  scene = eng::Engine::Get().GetScene();

    // ---- a FILE is selected, not an entity --------------------------------
    //
    // AFTER ApplyPendingRemoval and before anything reads a component list.
    // Returning here is safe for one reason and it is worth stating: the only
    // work this function owes anybody regardless of what is selected is that
    // pending removal, and it has already happened - which is exactly why
    // that call is the first line of this function rather than the last.
    //
    // ONE RULE, NO RECENCY. A file selected means a file shown; selecting an
    // entity clears it. See EditorState::selectedAsset.
    //
    // ======================================================================
    //  NOT WHILE SOMETHING IS BEING DRAGGED, and that condition is the whole
    //  reason this branch is not simply "is a file selected".
    //
    //  The Assets panel selects on mouse-DOWN - deliberately, so that a click
    //  which turns into a drag still selects what it started from. That is the
    //  SAME press that begins the drag. Without this test the Inspector
    //  switched to the asset view the instant an image was picked up, taking
    //  every drop target in it along too, so dragging an image from the Assets
    //  panel onto an entity here was not awkward, it was impossible.
    //
    //  While something is in flight, what you need to see is the thing you are
    //  dropping ONTO. ImGui::IsDragDropActive is from imgui_internal.h, which
    //  this file already includes for BeginDragDropTargetCustom.
    // ======================================================================
    if (!state.selectedAsset.empty() && !ImGui::IsDragDropActive()) {
        // A COPY, NOT THE MEMBER. Three things inside DrawAssetInspector
        // reassign selectedAsset - "Look at it", a frame clicked in the slice
        // preview, and "Open the sheet" - and it was being handed a reference
        // to the very string they write. The rest of the function then
        // described a different asset from the one it had started on, with the
        // header it had already drawn naming the old one. One copy a frame.
        const std::string selected = state.selectedAsset;
        DrawAssetInspector(selected);
        return;
    }

    // Looked up from the ID every frame. A selection that has been destroyed
    // comes back as nothing and is reported, rather than crashing - which is
    // the entire reason the selection is an id and not a pointer.
    eng::Entity* entity = scene.Get(state.selected);
    if (entity == nullptr) {
        if (!state.selected.IsNull()) {
            ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f),
                               "the selected entity no longer exists");
            if (ImGui::Button("Clear selection")) {
                state.ClearSelection();
            }
        } else {
            ImGui::TextDisabled("select an entity in the Hierarchy");
        }
        return;
    }

    // ---- the WHOLE PANEL is a drop target --------------------------------
    //
    // Not a strip or a header - the entire window, so an image or a script can
    // be dropped anywhere on it. Done before the fields are drawn so that a
    // widget directly under the cursor still wins.
    if (ImGui::BeginDragDropTargetCustom(ImGui::GetCurrentWindow()->Rect(),
                                         ImGui::GetID("##inspector_drop"))) {
        std::string message;
        for (const char* payloadId : {kPayloadTexture, kPayloadScript}) {
            const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(payloadId);
            if (payload == nullptr) {
                continue;
            }
            const auto* path = static_cast<const char*>(payload->Data);

            // THE ENTITY IS WHAT THIS PANEL SHOULD DESCRIBE NOW. The file was
            // selected on mouse-down, before the drag began; having landed it
            // on an entity, what somebody wants to look at is the entity that
            // just changed, not the file it came from.
            state.selectedAsset.clear();

            if (ApplyAssetToEntity(state.selected, path, message)) {
                ENGINE_LOG_INFO(eng::Channels::kEditor, "{}", message);
            } else {
                ENGINE_LOG_WARN(eng::Channels::kEditor, "{}", message);
            }
        }
        ImGui::EndDragDropTarget();
    }

    const bool running = !eng::Engine::Get().Clock().IsPaused();

    // ---- when several things are selected ---------------------------------
    //
    // The components below belong to the ACTIVE entity only, and saying so
    // matters: editing a field here with eight entities selected changes one
    // of them, and somebody who thought otherwise would not find out until
    // much later. The alternative - editing every selected entity at once -
    // is a real feature with real questions behind it (what happens when
    // they disagree?) and is not something to do by accident.
    if (state.selection.size() > 1) {
        ImGui::TextColored(ImVec4(0.55f, 0.78f, 0.95f, 1.0f), "%zu entities selected",
                           state.selection.size());

        if (ImGui::Button("Destroy all")) {
            // The same one the Delete key and Edit -> Delete use.
            DestroySelection();
        }

        if (ImGui::TreeNode("Show the list")) {
            for (const eng::EntityId id : state.selection) {
                if (const eng::Entity* member = scene.Get(id)) {
                    // Clicking one makes it the active entity, so the list
                    // doubles as a way of stepping through the selection
                    // without losing it.
                    if (ImGui::Selectable(member->Name().c_str(), id == state.selected)) {
                        state.selected = id;
                    }
                }
            }
            ImGui::TreePop();
        }

        ImGui::Separator();
        ImGui::TextDisabled("below: the components of the active entity only");
    }

    // The name in blue when this entity belongs to a prefab, exactly as the
    // Hierarchy colours its row - the one signal that says editing this is
    // editing one copy of something.
    if (scene.PrefabInstanceOf(entity->Id()) != nullptr) {
        ImGui::TextColored(kPrefabBlue, "%s", entity->Name().c_str());
    } else {
        ImGui::Text("%s", entity->Name().c_str());
    }

    // A PREFAB'S CHILD CANNOT BE DESTROYED ON ITS OWN, and the button says so
    // rather than refusing after the click. DestroyEntity refuses it too -
    // this is the explanation, not the enforcement. See IsPrefabChild.
    const bool locked = IsPrefabChild(entity->Id());

    ImGui::SameLine(ImGui::GetWindowWidth() - 90.0f);
    ImGui::BeginDisabled(locked);
    if (ImGui::Button("Destroy")) {
        // Answer 3: through the deferred queue, like everything else - and
        // through the one shared door, which is what marks the scene so undo
        // can see it. This used to call DeferredOps::QueueDestroy directly
        // and was the only destroy in the editor that did not mark. See
        // DestroyEntity in EditorApp.h.
        DestroyEntity(entity->Id());
    }
    ImGui::EndDisabled();
    if (locked && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", PrefabChildRefusal());
    }

    // ---- switched on, and what kind of thing it is -----------------------
    //
    // Both on one row, directly under the name, because both are properties
    // of the ENTITY rather than of any component - the same place Unity puts
    // them and the first place anybody looks.
    {
        bool active = entity->IsActiveSelf();
        if (ImGui::Checkbox("##active", &active)) {
            entity->SetActive(active);
            EditorState::Get().MarkDirty();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Switched on. An entity that is off keeps everything it has but "
                "nothing acts on it: it is not drawn, it does not collide, its "
                "scripts do not run, and it is invisible to every physics query.\n\n"
                "Switching off a parent switches off everything under it.");
        }

        // THE REASON IT LOOKS OFF WHEN THE BOX IS TICKED, said rather than
        // left to be puzzled over. Switching off a parent is the one way an
        // entity can be inactive while its own flag is true, and without this
        // line the Hierarchy greys it out for no visible reason.
        if (active && !entity->IsActiveInHierarchy()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.30f, 1.0f), "off: a parent is off");
        } else {
            ImGui::SameLine();
            ImGui::TextDisabled("on");
        }

        ImGui::SameLine();
        ImGui::TextUnformatted("Tag");
        ImGui::SameLine();

        char tagBuffer[64] = {};
        const std::string& tag = entity->Tag();
        std::snprintf(tagBuffer, sizeof(tagBuffer), "%s", tag.c_str());

        ImGui::SetNextItemWidth(130.0f);
        if (ImGui::InputText("##tag", tagBuffer, sizeof(tagBuffer))) {
            entity->SetTag(tagBuffer);
            EditorState::Get().MarkDirty();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "What KIND of thing this is - \"Player\", \"Enemy\", \"Pickup\".\n\n"
                "Unlike the name, a tag is not unique, so every bullet can carry "
                "the same one. Unlike a collision layer, it changes nothing about "
                "what this collides with, and it works on things that have no "
                "collider at all.\n\n"
                "A script asks entity->HasTag(\"Enemy\"), or "
                "FindEntityWithTag(\"Player\").");
        }
    }

    ImGui::Separator();

    // ---- which prefab, and what has been changed about this instance ------
    //
    // Before the components, because it is about all of them. The list comes
    // back so that the components below can mark themselves without running
    // the diff again.
    const std::vector<eng::PrefabOverrideItem> prefabOverrides =
        DrawPrefabBlock(*entity);
    const int prefabLocalId = scene.PrefabLocalIdOf(entity->Id());

    // WALKED BY INDEX rather than with ForEachComponent, because removing
    // needs to say WHICH component - not which type. An entity can hold
    // several scripts, and "remove the ScriptComponent" would always take the
    // first one however far down the list the x was clicked. See
    // Entity::RemoveComponentAt.
    //
    // Re-reading ComponentCount each time round is what makes it safe for the
    // body to add a component, which is the same reason ForEachComponent is
    // written this way. Removal is deferred and so does not come into it.
    // Which of several components of the same type this is - the second
    // ScriptComponent is slot 1 - because that is how an override names one.
    // Counted as the loop walks, which is the same order the diff walks in.
    std::map<std::string, int> slotsSeen;

    for (std::size_t i = 0; i < entity->ComponentCount(); ++i) {
        eng::Component* componentPtr = entity->ComponentAt(i);
        if (componentPtr == nullptr) {
            continue;
        }
        eng::Component& component = *componentPtr;
        const int       slot      = slotsSeen[component.TypeName()]++;

        // PushID makes every widget inside this block unique to this
        // component, so two components with a field called "Offset" do not end
        // up sharing one widget.
        //
        // Keyed on the ADDRESS rather than the index, so that a header you
        // opened stays open when something above it is removed and everything
        // below shifts up a place.
        ImGui::PushID(&component);

        const std::string_view typeName = component.TypeName();

        // THE TRANSFORM IS THE ONE THAT STAYS. Every entity has one by
        // definition; see the note in InspectorPanel.h.
        const bool removable = typeName != eng::TransformComponent::kTypeName;

        // A SCRIPT IS NAMED AFTER THE SCRIPT, not after the class that holds
        // it. "ScriptComponent" is an implementation detail of the engine;
        // what you attached was PlayerMovement, and that is what you are
        // looking for when you scan the panel.
        //
        // It also makes several scripts on one entity tellable apart. Three
        // headers all reading "ScriptComponent" mean opening each one to find
        // out which is which - and now that a header carries a remove button,
        // guessing is expensive.
        //
        // The "(Script)" suffix is how Unity writes the same thing, and it
        // earns its place: without it a bare "PlayerMovement" sitting between
        // "SpriteComponent" and "AABBColliderComponent" gives no clue what
        // kind of thing it is.
        //
        // EVERYTHING AFTER ### IS THE IDENTITY, and the part before it is
        // only what you read. ImGui normally derives a widget's identity from
        // its label, which would mean rebinding a script - renaming this
        // header - counted as a different header, and the one you had open
        // would spring back to its default. The visible text is now free to
        // change without the panel rearranging itself underneath you. See the
        // note about widget identity in Panel.h.
        char header[128];
        if (typeName == eng::ScriptComponent::kTypeName) {
            const std::string& scriptName =
                static_cast<eng::ScriptComponent&>(component).ScriptName();
            if (scriptName.empty()) {
                std::snprintf(header, sizeof(header),
                              "Script (nothing bound yet)###component");
            } else {
                std::snprintf(header, sizeof(header), "%s (Script)###component",
                              scriptName.c_str());
            }
        } else {
            std::snprintf(header, sizeof(header), "%s###component",
                          component.TypeName());
        }

        // CollapsingHeader draws its own x when given somewhere to report it,
        // which is the ordinary way a panel like this offers "remove". `keep`
        // comes back false on the frame the x is clicked.
        // BLUE WHEN THIS COMPONENT HAS BEEN CHANGED on this instance, so that
        // a collapsed one still says there is something inside it. Unity puts
        // the component name in bold blue for the same reason.
        const bool overridden =
            ComponentIsOverridden(prefabOverrides, prefabLocalId, typeName, slot);
        if (overridden) {
            ImGui::PushStyleColor(ImGuiCol_Text, kPrefabBlue);
        }

        bool       keep = true;
        const bool open = removable
                              ? ImGui::CollapsingHeader(header, &keep,
                                                        ImGuiTreeNodeFlags_DefaultOpen)
                              : ImGui::CollapsingHeader(header,
                                                        ImGuiTreeNodeFlags_DefaultOpen);

        if (overridden) {
            ImGui::PopStyleColor();
        }

        // The same thing by right-click, because an x is easy to miss and
        // impossible to find by looking for the word "remove". Attached to the
        // header, which is the item just drawn.
        if (ImGui::BeginPopupContextItem("component-actions")) {
            if (ImGui::MenuItem("Remove Component", nullptr, false, removable)) {
                keep = false;
            }
            if (!removable) {
                ImGui::TextDisabled("Every entity has a transform.");
            }
            ImGui::EndPopup();
        }

        if (!removable && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Every entity has a transform, so this one cannot "
                              "be removed.");
        }

        if (!keep) {
            // RECORDED, NOT DONE. This loop is walking the component list;
            // erasing from it here shifts every later component down a place
            // and the loop would step straight over one. Carried out by
            // ApplyPendingRemoval at the end of Draw.
            m_pendingRemoval.entity    = entity->Id();
            m_pendingRemoval.index     = i;
            m_pendingRemoval.requested = true;
        }

        if (open) {
            // Work out which kind of component this is and call the matching
            // draw function. Comparing type NAMES is the same way the scene
            // file identifies components.
            const std::string_view type = typeName;

            if (type == eng::TransformComponent::kTypeName) {
                DrawTransform(static_cast<eng::TransformComponent&>(component), running);
            } else if (type == eng::SpriteComponent::kTypeName) {
                DrawSprite(static_cast<eng::SpriteComponent&>(component));
            } else if (type == eng::BoxColliderComponent::kTypeName ||
                       type == eng::CircleColliderComponent::kTypeName ||
                       type == eng::CapsuleColliderComponent::kTypeName ||
                       type == eng::PolygonColliderComponent::kTypeName ||
                       type == eng::EdgeColliderComponent::kTypeName ||
                       type == eng::TilemapColliderComponent::kTypeName) {
                DrawCollider(static_cast<eng::ColliderComponent&>(component), scene);
            } else if (type == eng::Rigidbody2DComponent::kTypeName) {
                DrawRigidbody(static_cast<eng::Rigidbody2DComponent&>(component));
            } else if (type == eng::TilemapComponent::kTypeName) {
                DrawTilemap(static_cast<eng::TilemapComponent&>(component));
            } else if (type == eng::SpriteAnimationComponent::kTypeName) {
                DrawSpriteAnimation(
                    static_cast<eng::SpriteAnimationComponent&>(component));
            } else if (type == eng::SpinComponent::kTypeName) {
                DrawSpin(static_cast<eng::SpinComponent&>(component));
            } else if (type == eng::ScriptComponent::kTypeName) {
                DrawScript(static_cast<eng::ScriptComponent&>(component));
            } else if (type == eng::CameraComponent::kTypeName) {
                DrawCamera(static_cast<eng::CameraComponent&>(component));
            } else if (type == eng::UIRectComponent::kTypeName) {
                DrawUIRect(static_cast<eng::UIRectComponent&>(component));
            } else if (type == eng::UITextComponent::kTypeName) {
                DrawUIText(static_cast<eng::UITextComponent&>(component));
            } else if (type == eng::UIImageComponent::kTypeName) {
                DrawUIImage(static_cast<eng::UIImageComponent&>(component));
            } else if (type == eng::UIButtonComponent::kTypeName) {
                DrawUIButton(static_cast<eng::UIButtonComponent&>(component));
            } else if (type == eng::UISliderComponent::kTypeName) {
                DrawUISlider(static_cast<eng::UISliderComponent&>(component));
            } else {
                // Not an error. Somebody may write their own component type,
                // and the Inspector should say so rather than pretend it is
                // not there.
                ImGui::TextDisabled("no editor written for this component type yet");
            }

            // AFTER the fields, so the list of what differs reads as a summary
            // of what is above it rather than as a header for it.
            DrawComponentOverrides(prefabOverrides, entity->Id(), prefabLocalId,
                                   typeName, slot);
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    ImGui::SeparatorText("Add component");

    // The list of buttons is generated from the factory, so a component type
    // registered anywhere appears here with no edit to this file.
    eng::ComponentFactory::ForEachType([&](const char* typeName) {
        // ======================================================================
        //  ONE IS USUALLY ALL THERE IS, AND SOMETIMES IT IS NOT.
        //
        //  This used to skip any type the entity already had, full stop. That
        //  is right for a transform, a sprite, a rigidbody or a camera, and
        //  WRONG for a collider and for a script: a compound body is several
        //  colliders on one entity, physics/Collider.h recommends it, the
        //  collision system reports one event per collider pair so that it
        //  works, and a scene file could always do it. Only this menu could
        //  not, so a compound body was loadable and not buildable.
        //
        //  THE EXISTING COMPONENT IS WHAT ANSWERS. There is nothing to ask
        //  until the entity has one, and the one it has is the right thing to
        //  ask - see Component::AllowsSeveral.
        // ======================================================================
        const eng::Component* existing = entity->FindComponent(typeName);
        if (existing != nullptr && !existing->AllowsSeveral()) {
            return;
        }
        char label[96];
        std::snprintf(label, sizeof(label), "+ %s", typeName);
        if (ImGui::SmallButton(label)) {
            entity->AddComponent(typeName);
            EditorState::Get().MarkDirty();
        }
    });
}

} // namespace editor
