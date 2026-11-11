#pragma once

// ============================================================================
//  ScriptFields.h - the list of variables the Inspector draws for a script.
//
//  MOST SCRIPTS NEVER TOUCH THIS FILE. A public variable of a supported type
//  appears in the Inspector on its own:
//
//      class Orbiter final : public eng::ScriptBehaviour {
//      public:
//          float radius = 90.0f;   // how far out it orbits
//          float speed  = 1.2f;    // [0..10] turns per second
//
//          void OnFixedUpdate() { ... }
//      };
//
//  That is done by a BUILD STEP that reads the source, because C++ cannot ask
//  a class what members it has - see engine/tools/ScriptVars.h for how, and
//  for what it deliberately does not understand.
//
//  This file is the model underneath: the list that step fills in, what the
//  scene file stores, and the escape hatch for the cases it cannot cover.
//
//  ==========================================================================
//  THE ESCAPE HATCH: OnExpose
//
//  A script can still write the list itself, and when it does, THE
//  HAND-WRITTEN ONE WINS - nothing generated argues with it:
//
//      void OnExpose(eng::ScriptFields& fields) {
//          fields.Var("radius", radius, "how far out it orbits");
//          fields.Range("speed", speed, 0.0f, 10.0f);
//      }
//
//  Worth reaching for when the list is not a fixed set of declarations: a
//  variable exposed only in some builds, a name worked out at run time, or a
//  type the parser cannot see through. Nothing in assets/scripts needs it any
//  more, which is the point.
//
//  ==========================================================================
//  WHY IT IS A SOURCE PARSE AND NOT REFLECTION
//
//  Every other question the engine asks about a script - "does it have an
//  OnUpdate?" - is answered by the compiler. See ScriptHooks.h. This one
//  cannot be:
//
//  C++ CAN ASK WHETHER A NAME EXISTS. IT CANNOT ASK WHAT NAMES EXIST.
//
//  `requires(T& t) { t.OnUpdate(); }` works because OnUpdate is spelled out in
//  the question. There is no way to write "list the members of T" - no
//  reflection, in any released C++ standard. So something has to read the
//  source, which is what Unreal does and what the editor does here.
//
//  THE COST OF THAT CHOICE IS THE ONE UNITY HAS TOO: every public field of a
//  supported type is exposed whether or not it was meant to be. TWO ways to
//  keep one off screen: make it PRIVATE, or write OnExpose and leave it out.
//
//  AN m_ PREFIX DOES NOT HIDE ANYTHING, and this file used to say it did.
//  It was read that way once - this project's own naming convention taken as
//  [HideInInspector] - and that was a trap, because half the C++ world names
//  ordinary members m_something and got no box, no skipped report and no way
//  to find out why. `private:` is the language's own way of saying "not part
//  of the interface" and cannot be arrived at by accident. The long note in
//  engine/src/tools/ScriptVars.cpp has the argument.
//
//  ==========================================================================
//  WHAT THE SCENE FILE STORES, AND WHAT IT DELIBERATELY DOES NOT
//
//  ONLY THE VALUES SOMEBODY CHANGED. A field left alone is absent from the
//  file, and the script's own initialiser - `float radius = 90.0f` - is what
//  it gets.
//
//  That is the opposite of Unity, which writes every field of every component
//  into the scene, and it is worth being deliberate about because it decides
//  something awkward: WHAT HAPPENS WHEN YOU CHANGE A DEFAULT IN THE CODE.
//
//  Storing everything means the new default reaches nothing. Twenty entities
//  carry a copy of the old one, the code says 90, the game does 60, and the
//  only fix is to touch all twenty. Storing only overrides means changing the
//  line changes every entity that never disagreed with it, which is what
//  somebody editing a default is actually asking for.
//
//  The cost is that "the value in the file" and "the value you see" are not
//  the same set, so the Inspector marks the ones that have been overridden and
//  offers to put the default back. Being able to say "no, use the code's
//  value" is only possible BECAUSE the file is sparse.
//
//  ==========================================================================
//  A VALUE OUTLIVES THE SCRIPT IT BELONGS TO
//
//  The saved values are held by the COMPONENT, not by the behaviour object,
//  and they survive two things that destroy the behaviour:
//
//    - A SCRIPT RELOAD. The editor recompiles scripts and unloads the old
//      library, taking every behaviour object with it. See ScriptLibrary.h.
//    - A SCRIPT THAT IS NOT COMPILED IN. Attach "PlayerController" in a build
//      without it and the component holds its values untouched, saves them,
//      and hands them over on the build that has the class.
//
//  The second one is the reason this is stored as JSON rather than as typed
//  values. With no class to ask, there is no way to know that `speed` is a
//  float - so the text is kept exactly as it was read and written back out
//  unchanged. Saving a scene in a build that is missing a script does not
//  quietly empty that script's settings.
// ============================================================================

#include <engine/core/Json.h>
#include <engine/math/Vec2.h>
#include <engine/render/Renderer.h>
#include <engine/scene/AnimationRef.h>
#include <engine/scene/ComponentRef.h>
#include <engine/scene/EntityRef.h>
#include <engine/scene/ImageRef.h>
#include <engine/scene/PrefabRef.h>

#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace eng {

// ---------------------------------------------------------------------------
//  The types a script variable may have.
//
//  A SHORT LIST ON PURPOSE. Every entry here is a widget the Inspector has to
//  draw and a shape the scene file has to store, so each one has to earn its
//  place. They cover what scripts in this project actually tune: a distance, a
//  count, a switch, a label, a position, a colour, and the four that are not
//  values at all - another entity, a prefab to spawn, an animation to play and
//  a picture to draw.
//
//  NEW VALUES ARE APPENDED AND NEVER INSERTED, and that is not a style rule.
//  A script lives in its own DLL and ScriptField's accessors are a tag test
//  followed by an UNCHECKED cast - `type == Prefab ? static_cast<PrefabRef*>`.
//  Renumbering means a library built against the old order hands a Vec2 to a
//  function that will call Path() on it, which is not a crash, it is a
//  plausible-looking string written into somebody's scene file.
//
//  An enum rather than std::type_info, because a script lives in its own DLL:
//  comparing typeid across two binaries is a question about how the linker
//  felt that day, and this is a number that means the same thing everywhere.
// ---------------------------------------------------------------------------
enum class ScriptFieldType {
    Float,
    Int,
    Bool,
    String,
    Vec2,
    Color,

    // ANOTHER ENTITY - the only one that is not a value.
    //
    // It is an eng::EntityRef, which holds a NAME and looks the entity up when
    // asked. The Inspector draws it as something you drag an entity onto from
    // the Hierarchy, and it can be constrained to entities carrying a
    // particular component or script - see ScriptFields::Ref. EntityRef.h has
    // the argument for why a name and not a pointer.
    Entity,

    // A PREFAB TO SPAWN. The other one that is not a value.
    //
    // It is an eng::PrefabRef, which holds a virtual path. The Inspector draws
    // it as a slot you drag a prefab onto from the Assets panel, and says so
    // when the file it names is not there. See PrefabRef.h for why a spawner
    // written with a plain std::string is worse in four separate ways.
    Prefab,

    // AN ANIMATION TO PLAY. The third one that is not a value, and the newest.
    //
    // It is an eng::AnimationRef, which holds the sub-asset path of one clip
    // inside one sheet - "textures/hero.png#walk_left". The Inspector draws it
    // as the same slot a Sprite Animation component's own clip gets, with the
    // project's clips in a picker, and paints it red when no sheet holds the
    // animation it names.
    //
    // THE OLDER SPELLING WAS `std::string x; // [animation]`, and it still
    // works - see AssetFieldKind above. What it could not do was be guessed:
    // an animation was the only thing in this engine designated by a comment,
    // so a second one added beside the first silently came out as a text box.
    // AnimationRef.h has the argument; SpriteAnimationComponent::Play(const
    // AnimationRef&) has the twenty-five lines of script-side path-vs-name
    // reasoning it absorbs.
    //
    // APPENDED, for the reason in the note above this enum.
    Animation,

    // A PICTURE TO DRAW. The fourth that is not a value, and AnimationRef's
    // twin.
    //
    // It is an eng::ImageRef, which holds the virtual path of a whole image -
    // "textures/ui/key.png" - or of one part of a sliced sheet -
    // "textures/ui/items.png#key". The Inspector draws it as the same slot a
    // Sprite component's texture gets, with the project's images and every
    // sliced frame in a picker, and paints it red when the file is gone or the
    // sheet has no part by that name.
    //
    // THE OLDER SPELLING WAS `std::string x; // [image]`, and it still works.
    // Unlike [animation], NOTHING IN THIS PROJECT EVER USED IT - which is the
    // weakest state a feature can be in: documented, unexercised, and
    // discoverable only by reading this header.
    //
    // ImageRef.h has the argument, including why this is a SECOND TYPE rather
    // than AnimationRef carrying a kind: a bare string means opposite things
    // in the two of them, so the accessors come out inverted rather than
    // shared.
    //
    // APPENDED, for the reason in the note above this enum.
    Image,
};

// "float", "colour" - for error messages and the Inspector's tooltips.
const char* ScriptFieldTypeName(ScriptFieldType type);

// ---------------------------------------------------------------------------
//  WHAT KIND OF ASSET A STRING NAMES, and the ONE list that decides it.
//
//  `// [animation]` on a std::string is what turns its text box into the slot
//  a Sprite's texture gets. THE MECHANISM IS GENERAL: it works on any public
//  std::string, and on any fixed-size array of them, in any script. Nothing
//  anywhere knows the name of a particular variable - which is worth stating
//  plainly, because the opposite was reported. A second array of animations
//  added beside one that had the marker came out as plain text boxes, and the
//  only possible conclusion from the outside was that the first one had been
//  wired in by hand.
//
//  ==========================================================================
//  WHY THIS LIVES IN THE ENGINE AND NOT IN THE INSPECTOR
//
//  There used to be TWO lists of accepted spellings: one in AddAsset below,
//  deciding whether to warn, and one in the editor's InspectorPanel, deciding
//  whether to draw a slot. They agreed, and nothing kept them agreeing.
//
//  Adding a kind to the editor's list alone would draw a slot for a marker
//  the engine had just warned about. Adding it here alone would accept the
//  marker silently and then draw a text box - which is the failure this
//  marker already has too much of.
//
//  So the accepted spellings are decided once, here, and the editor maps the
//  ANSWER onto its own widget. Its switch has no `default:`, so a kind added
//  below is a compiler warning there rather than a silent text box.
//
//  ==========================================================================
//  CASE-INSENSITIVE, AND WITH THE SPELLINGS SOMEBODY ACTUALLY TYPES.
//
//  `[Animation]` used to fall through to an ordinary text box. It was
//  reported in the Console, which is better than nothing, but a marker is
//  prose inside a comment and nobody capitalises prose consistently.
//
//  The required type on an EntityRef - `[Rigidbody2DComponent]` - is a C++
//  name and stays case-SENSITIVE, because it has to match one. This is not a
//  C++ name and does not have to match anything, so it does not need to be.
//
//  ==========================================================================
//  BOTH KINDS NOW HAVE A TYPE, so this whole mechanism is the OLDER SPELLING.
//
//  eng::AnimationRef and eng::ImageRef say in the DECLARATION what a marker
//  says in a comment, and they are what a new script should reach for. See
//  AnimationRef.h and ImageRef.h for the argument.
//
//  NOTHING HERE IS DEPRECATED IN THE SENSE OF WARNING. A script written with
//  a marker is correct - here and in anybody else's project - and draws the
//  same slot it always did. Removing this would break working scripts to save
//  nothing.
// ---------------------------------------------------------------------------
enum class AssetFieldKind {
    None,        // not a kind this engine can draw a slot for
    Image,       // any picture, including one frame cut out of a sliced sheet
    Animation,   // a named animation stored inside a sheet, and nothing else
};

// Ignores case. Returns None for anything that is not one of the spellings
// AssetFieldKindNames() lists - including an EntityRef's required type, which
// arrives here whenever somebody writes one on a string by mistake.
AssetFieldKind AssetFieldKindFor(std::string_view marker);

// "image", "animation", or empty for None. THE CANONICAL SPELLING, which is
// what a field stores however the marker was typed - so everything downstream
// compares against one word rather than six.
std::string_view AssetFieldKindName(AssetFieldKind kind);

// Every accepted spelling, for the messages and tooltips that have to list
// them. One source, so a message cannot under-report what the code takes.
std::string_view AssetFieldKindNames();

// ---------------------------------------------------------------------------
//  One variable a script has offered up.
// ---------------------------------------------------------------------------
struct ScriptField {
    // COPIES, not string_views into the script's own binary.
    //
    // The text passed to Var() is a literal inside the compiled script
    // library, and that library is unloaded and replaced whenever scripts are
    // rebuilt. A view would be left pointing into memory that has been
    // unmapped - and it would read correctly for a while first, which is the
    // worst way for a bug like this to behave. Copying costs one allocation
    // per variable per bind and removes the whole problem.
    std::string name;
    std::string tooltip;

    ScriptFieldType type = ScriptFieldType::Float;

    // Points AT THE MEMBER of the live behaviour object. Valid for exactly as
    // long as that object is bound - ScriptComponent clears the whole list in
    // Unbind, for the same reason the hook table is cleared there.
    void* value = nullptr;

    // Set by Range(), and only meaningful for Float and Int. A ranged field is
    // drawn as a slider with ends rather than a drag that goes on forever,
    // which is what you want for something like a volume between 0 and 1.
    bool  ranged = false;
    float min    = 0.0f;
    float max    = 0.0f;

    // WHAT THE MARKER IN THE COMMENT SAID, and it means two different things
    // depending on the type in front of it:
    //
    //   on an Entity   a component type name or a script name the target has
    //                  to carry - "Rigidbody2DComponent", "DoorSwitch"
    //   on a String    a kind of asset - "image" or "animation", canonical,
    //                  whatever spelling the marker used. See AssetFieldKind.
    //
    // AND EMPTY ON AN Animation OR AN Image FIELD, which is worth stating
    // because those look like the two places it would be full. A typed
    // eng::AnimationRef or eng::ImageRef carries its meaning in its TYPE, so
    // there is no marker to record: "animation" and "image" can only arrive
    // in this string from the older std::string spelling.
    //
    // The parser enforces it - it clears this for every type that is neither
    // Entity nor String - and that filter is load-bearing: a leftover
    // `// [animation]` on an AnimationRef would otherwise drive the generator
    // to emit f.Ref(..., AnimationRef&, "animation"), which is ill-formed and
    // would make the field vanish rather than fail.
    //
    // Empty for everything else, and for "any entity will do". The Inspector
    // asks the field's TYPE before reading this, because the two meanings
    // share one string: a tooltip that read "an entity with a animation on
    // it" is what that costs when it is forgotten.
    //
    // A NAME AND NOT A TYPE, which is what lets one mechanism serve both a
    // component the engine ships and a script somebody wrote this afternoon.
    // Both are registered under a name and neither can be named in a header
    // that every script includes - a script class lives in an anonymous
    // namespace, so no other file can even mention it.
    //
    // It is advice to the Inspector, not a rule the engine keeps: a reference
    // can be pointed at an entity that does not satisfy it, and the Inspector
    // says so rather than refusing. A level in the middle of being built is
    // allowed to be wrong.
    //
    // NOT called `requires`, which has been a C++ KEYWORD since C++20 - the
    // same keyword ScriptHooks.h uses to ask whether a script has an OnUpdate.
    std::string requiredType;

    // ---- typed access ----------------------------------------------------
    //
    // Each returns null when the field is NOT that type, rather than casting
    // regardless. A caller that mixes up two fields then reads nothing
    // instead of reinterpreting a std::string as a float, which is the kind
    // of mistake that corrupts memory rather than reporting itself.
    float*       AsFloat() const;
    int*         AsInt() const;
    bool*        AsBool() const;
    std::string* AsString() const;
    Vec2*        AsVec2() const;
    Color*       AsColor() const;
    EntityRef*   AsEntityRef() const;
    PrefabRef*   AsPrefabRef() const;

    // Longer than its neighbours, so these two break the column rather than
    // widening the eight above that were fine.
    AnimationRef* AsAnimationRef() const;
    ImageRef*     AsImageRef() const;
};

namespace fields {

// Which ScriptFieldType a C++ type maps to. `known` is false for everything
// not listed, which is what turns an unsupported variable into a readable
// compile error instead of a confusing one about an incomplete type.
template <class T>
struct Traits {
    static constexpr bool known = false;
};

template <> struct Traits<float> {
    static constexpr bool            known = true;
    static constexpr ScriptFieldType type  = ScriptFieldType::Float;
};
template <> struct Traits<int> {
    static constexpr bool            known = true;
    static constexpr ScriptFieldType type  = ScriptFieldType::Int;
};
template <> struct Traits<bool> {
    static constexpr bool            known = true;
    static constexpr ScriptFieldType type  = ScriptFieldType::Bool;
};
template <> struct Traits<std::string> {
    static constexpr bool            known = true;
    static constexpr ScriptFieldType type  = ScriptFieldType::String;
};
template <> struct Traits<Vec2> {
    static constexpr bool            known = true;
    static constexpr ScriptFieldType type  = ScriptFieldType::Vec2;
};
template <> struct Traits<Color> {
    static constexpr bool            known = true;
    static constexpr ScriptFieldType type  = ScriptFieldType::Color;
};
template <> struct Traits<EntityRef> {
    static constexpr bool            known = true;
    static constexpr ScriptFieldType type  = ScriptFieldType::Entity;
};
template <> struct Traits<PrefabRef> {
    static constexpr bool            known = true;
    static constexpr ScriptFieldType type  = ScriptFieldType::Prefab;
};
// ONE SPECIALIZATION FOR EVERY COMPONENT TYPE, which is why it is partial.
//
// A ComponentRef IS an EntityRef - it derives from one, adds no members and
// has no vtable - so it is stored, serialized, retargeted on a rename and
// drawn by everything that already handles ScriptFieldType::Entity, and
// AsEntityRef's unchecked cast stays valid because the base sits at offset 0.
// The component type it is constrained to does not travel in the enum, which
// holds one number per KIND; it travels in ScriptField::requiredType, which
// the generated exposer fills in from the template argument.
template <class T> struct Traits<ComponentRef<T>> {
    static constexpr bool            known = true;
    static constexpr ScriptFieldType type  = ScriptFieldType::Entity;
};
// THE WHOLE OF WHAT MAKES AnimationRef A FIRST-CLASS FIELD. Two independent
// consumers need it and neither says so out loud if it is missing: Var's
// `if constexpr (Traits<Clean>::known)` below, and the generated exposer's
// nested requirement in ScriptVars.cpp. Without this specialization the
// generated lambda compiles away and the variable simply never appears.
template <> struct Traits<AnimationRef> {
    static constexpr bool            known = true;
    static constexpr ScriptFieldType type  = ScriptFieldType::Animation;
};
// AND THE SAME FOR ImageRef, with the same two silent consumers: Var's
// `if constexpr (Traits<Clean>::known)` and the generated exposer's nested
// requirement in ScriptVars.cpp. Forget this and the generated lambda
// compiles away - a clean build, and the variable simply never appears.
template <> struct Traits<ImageRef> {
    static constexpr bool            known = true;
    static constexpr ScriptFieldType type  = ScriptFieldType::Image;
};

// A slider needs two ends to put them at, so only the numeric types can have
// one. Named separately from `known` so the two mistakes get two messages.
template <class T>
constexpr bool CanHaveRange =
    std::is_same_v<T, float> || std::is_same_v<T, int>;

} // namespace fields

// ---------------------------------------------------------------------------
//  What OnExpose is handed.
//
//  It is filled in once, when the script is bound, and then read by the
//  Inspector and by the save code. A script's OnExpose should do nothing but
//  list variables - it is not a hook for logic, and it runs at a moment when
//  the entity is set up but OnStart has not happened yet.
// ---------------------------------------------------------------------------
class ScriptFields {
public:
    // ONE VARIABLE, drawn as a drag box.
    //
    // The name is what appears in the Inspector and what goes in the scene
    // file, so RENAMING IT ORPHANS WHAT WAS SAVED - the old key stays in the
    // file and the new field falls back to its default. The Inspector says so
    // rather than leaving it to be noticed, which is as much as anything can
    // do without being told the two names are the same variable.
    //
    // The tooltip is optional and worth writing. This project's scripts are
    // read by people learning the engine, and "90 what?" is a question a line
    // of text answers permanently.
    template <class T>
    void Var(std::string_view name, T& value, std::string_view tooltip = "") {
        using Clean = std::remove_cv_t<T>;
        static_assert(fields::Traits<Clean>::known,
                      "a script variable must be one of: float, int, bool, "
                      "std::string, eng::Vec2, eng::Color, eng::EntityRef, "
                      "eng::PrefabRef, eng::AnimationRef, eng::ImageRef, or a "
                      "reference to a component - eng::ComponentRef<T>, which is "
                      "what Transform, SpriteRenderer, Rigidbody2D and Collider2D "
                      "are - or "
                      "a FIXED-SIZE ARRAY of one of those, "
                      "which the exposer turns into one row per element. Those are "
                      "the ones the Inspector can draw and the scene file can "
                      "store. A GROWABLE list is still out: a std::vector has no "
                      "length until the game runs, so there is nothing to lay out "
                      "and no key to save it under. Expose the parts of anything "
                      "else you want to tune.");

        // `if constexpr` and not just the static_assert above: without it, a
        // bad type ALSO produces "Traits<X> has no member 'type'", and the
        // second error buries the first one that actually says what to do.
        if constexpr (fields::Traits<Clean>::known) {
            Add(name, fields::Traits<Clean>::type, &value, tooltip, false, 0.0f, 0.0f);
        }
    }

    // THE SAME, DRAWN AS A SLIDER with `min` and `max` at its ends.
    //
    // Use it when the range is part of what the number means - a 0-to-1
    // blend, a percentage - and not merely to suggest a sensible size. A drag
    // box has no ends, and for a distance or a speed that is the honest
    // widget.
    //
    // THE ENDS ARE NOT ENFORCED anywhere but the widget. A script may set the
    // variable to anything it likes at run time, and a hand-edited scene file
    // may hold anything at all. Clamping on load was considered and rejected:
    // silently changing a number somebody typed into a file is worse than
    // showing a slider pinned at one end.
    template <class T>
    void Range(std::string_view name, T& value, T min, T max,
               std::string_view tooltip = "") {
        using Clean = std::remove_cv_t<T>;
        // THIS LIST WAS ALREADY TWO TYPES OUT OF DATE before AnimationRef -
        // it never gained EntityRef or PrefabRef - so somebody writing Range
        // on a reference was told their correct type was not a script variable
        // at all. The second assert below is the one that answers them; this
        // one only fires for a type the engine really does not know.
        static_assert(fields::Traits<Clean>::known,
                      "a script variable must be one of: float, int, bool, "
                      "std::string, eng::Vec2, eng::Color, eng::EntityRef, "
                      "eng::PrefabRef, eng::AnimationRef, eng::ImageRef.");
        static_assert(fields::CanHaveRange<Clean>,
                      "only a float or an int can have a Range - a slider needs two "
                      "ends to put them at. Use Var() instead.");
        static_assert(!std::is_const_v<T>,
                      "this variable is const, so the slider could show it but never "
                      "move it. Write it as an ordinary member - `float speed = 1.2f` "
                      "- rather than a static constexpr constant.");

        if constexpr (fields::Traits<Clean>::known && fields::CanHaveRange<Clean> &&
                      !std::is_const_v<T>) {
            Add(name, fields::Traits<Clean>::type, &value, tooltip, true,
                static_cast<float>(min), static_cast<float>(max));
        }
    }

    // A REFERENCE TO ANOTHER ENTITY, narrowed to the ones that can actually
    // do the job.
    //
    //     eng::EntityRef target;
    //     fields.Ref("target", target, "Rigidbody2DComponent", "what to push");
    //
    // `requiredType` is a COMPONENT TYPE NAME or a SCRIPT NAME, and the
    // Inspector uses it twice: the picker offers only entities that carry one,
    // and a reference pointing at something that does not say so in red. That
    // turns "why is nothing happening" into something visible on the entity
    // you are already looking at.
    //
    // IT IS MANDATORY HERE, with no default, and that is deliberate. A
    // defaulted third argument would sit exactly where Var() takes its
    // tooltip, so `fields.Ref("target", target, "what to chase")` would
    // quietly become a constraint on a type called "what to chase" and the
    // picker would offer nothing. Use Var() for an unconstrained reference -
    // it accepts an EntityRef like any other variable - and this when the
    // target has to have something on it.
    //
    // An unrecognised name is reported rather than ignored, which is what
    // catches the mistake above anyway. See the note on Add().
    void Ref(std::string_view name, EntityRef& value, std::string_view requiredType,
             std::string_view tooltip = "") {
        AddRef(name, &value, requiredType, tooltip);
    }

    // ========================================================================
    //  A STRING THAT HOLDS AN ASSET'S PATH, drawn as a slot rather than a box.
    //
    //      std::string walkAnimations[4];   // [animation] by direction
    //
    //  `kind` comes from that marker, and AssetFieldKindFor above is what
    //  decides whether it is one - case-insensitively, and accepting the
    //  spellings somebody actually types. The Inspector then draws the same
    //  slot a Sprite's texture gets - the file's NAME, a drop target, and a
    //  picker - instead of a path somebody has to type correctly from memory,
    //  which is how this was reported: the boxes could not be filled in at
    //  all.
    //
    //  ON AN ARRAY IT APPLIES TO EVERY ELEMENT, because the generated exposer
    //  calls this once per element with the same kind. Four boxes become four
    //  pickers and there is nothing per-element to write.
    //
    //  WHY A SEPARATE CALL AND NOT A MARKER ON Var. Var is a template over
    //  ten types and its third argument is the tooltip; adding a fourth
    //  meaning for one of those types would make `Var(name, value, "walk")`
    //  mean two different things depending on the type in front of it. This is
    //  the same split Ref already makes for the same reason.
    //
    //  AN UNKNOWN KIND IS A WARNING AND NOT A REFUSAL, exactly as a required
    //  type that names nothing is: the variable is kept and drawn as an
    //  ordinary string, so a typo in a marker costs a message rather than a
    //  variable.
    //
    //  THE TYPED SPELLING IS THE ONE TO REACH FOR NOW: eng::AnimationRef or
    //  eng::ImageRef as the member's type, and no comment at all. This call
    //  stays because the scripts that use a marker are correct and go on
    //  working - see the note above AssetFieldKind.
    // ========================================================================
    void Asset(std::string_view name, std::string& value, std::string_view kind,
               std::string_view tooltip = "") {
        AddAsset(name, &value, kind, tooltip);
    }

    // A variable that is const cannot be edited, and a slider that cannot
    // move is a worse answer than a compile error saying why.
    //
    // This overload exists ONLY to produce that error. A non-const variable
    // picks the one above - overload resolution prefers `T&` for a non-const
    // lvalue - so this is reached only by the mistake it describes.
    template <class T>
    void Var(std::string_view /*name*/, const T& /*value*/,
             std::string_view /*tooltip*/ = "") {
        static_assert(sizeof(T) == 0,
                      "this variable is const, so the Inspector could show it but "
                      "never change it. `static constexpr float kRadius = 90.0f` is "
                      "a constant and belongs in the code; to make it tunable, write "
                      "`float radius = 90.0f` as an ordinary member.");
    }

    // In DECLARATION ORDER - the order OnExpose called Var, which is the order
    // the Inspector draws them in. Worth relying on: a script can group
    // related settings together simply by listing them together.
    const std::vector<ScriptField>& All() const { return m_fields; }

    std::size_t      Count() const { return m_fields.size(); }
    bool             Empty() const { return m_fields.empty(); }

    // Null when no variable has that name.
    const ScriptField* Find(std::string_view name) const;

    void Clear() { m_fields.clear(); }

private:
    // THE ONE FUNCTION THAT TOUCHES THE VECTOR, and it lives in the engine
    // rather than in this header on purpose.
    //
    // Var() and Range() are templates, so they are compiled INTO THE SCRIPT'S
    // OWN LIBRARY. Add() is not, so the vector is only ever grown by engine
    // code - which keeps a container owned by one binary from being resized by
    // another. The same care ScriptRegistry::Register takes by accepting a
    // string_view instead of a std::string.
    void Add(std::string_view name, ScriptFieldType type, void* value,
             std::string_view tooltip, bool ranged, float min, float max);

    // Ref's half of the same job. Separate because it carries a string rather
    // than two floats, and because it is the one that checks its constraint
    // against what is actually registered.
    void AddRef(std::string_view name, EntityRef* value,
                std::string_view requiredType, std::string_view tooltip);

    // Asset's half, for the same reason: it carries a string rather than two
    // floats, and it is the one that checks the kind it was given.
    void AddAsset(std::string_view name, std::string* value, std::string_view kind,
                  std::string_view tooltip);

    std::vector<ScriptField> m_fields;
};

// ---------------------------------------------------------------------------
//  Moving one value between a field and JSON.
//
//  The shapes match what the rest of the engine already uses, so a scene file
//  stays consistent and hand-editable: a Vec2 is [x, y] as ReadVec2 expects,
//  and a Color is [r, g, b, a] from 0 to 255 as SpriteComponent's tint is.
//
//  An ENTITY REFERENCE is the target's name as a plain string - "Player" -
//  which is the same thing the Hierarchy shows and what somebody editing the
//  file by hand would type. An empty string means nothing is referred to.
// ---------------------------------------------------------------------------

// Reads `value` into the field. False means the JSON was the wrong shape, with
// outError saying what was expected - and in that case THE FIELD IS LEFT
// ALONE, so a bad line in a scene file costs you that one value's override and
// not the script's own default as well.
//
// Numbers are taken liberally: an int field accepts 3.0 and a float field
// accepts 3, because a person writing JSON by hand writes `"radius": 120` and
// being strict about that would be pedantry rather than safety.
//
// IT NEVER THROWS, whatever is in `value`. That is the same promise the
// readers in Json.h make, and for the same reason: this is reading a file
// somebody typed, and an exception out of the middle of loading a level is a
// level that will not open rather than a value that fell back.
bool ReadFieldFromJson(const ScriptField& field, const Json& value,
                       std::string& outError);

// The field's current value, in the shape above.
Json FieldToJson(const ScriptField& field);

// Could `value` be read into a field of this type? Used by the Inspector to
// mark a saved value that does not fit, without having to try it and undo it.
bool JsonFitsFieldType(ScriptFieldType type, const Json& value);

} // namespace eng
