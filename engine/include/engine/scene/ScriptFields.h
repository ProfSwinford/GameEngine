#pragma once

// ============================================================================
//  ScriptFields.h - showing a script's variables in the Inspector.
//
//  A script lists the variables it wants on screen, and the editor draws them,
//  saves them into the scene file, and puts them back when the scene loads:
//
//      class Orbiter final : public eng::ScriptBehaviour {
//      public:
//          float radius = 90.0f;
//          float speed  = 1.2f;
//
//          void OnExpose(eng::ScriptFields& fields) {
//              fields.Var("radius", radius, "how far out it orbits");
//              fields.Range("speed", speed, 0.0f, 10.0f);
//          }
//
//          void OnFixedUpdate() { ... }
//      };
//
//  That is the whole feature. Everything below is why it has to be written out
//  rather than discovered, and what the editor can be trusted to do with it.
//
//  ==========================================================================
//  WHY THE LIST IS WRITTEN BY HAND
//
//  Every other question the engine asks about a script - "does it have an
//  OnUpdate?" - is answered by the compiler, with no list to keep up to date.
//  See ScriptHooks.h. This one cannot be, and the difference is not laziness:
//
//  C++ CAN ASK WHETHER A NAME EXISTS. IT CANNOT ASK WHAT NAMES EXIST.
//
//  `requires(T& t) { t.OnUpdate(); }` works because OnUpdate is spelled out in
//  the question. There is no way to write "list the members of T" - no
//  reflection, in any released C++ standard - so a float called `radius` is
//  invisible to the engine until something names it. C# engines get this free;
//  this is the price of scripts being compiled rather than interpreted, and it
//  is one line per variable.
//
//  IT IS ALSO NOT PURELY A COST. A Unity script exposes every public field
//  whether or not it was meant for the Inspector, and hiding one needs an
//  attribute. Here a variable is on screen because somebody said so, which
//  means the Inspector shows the knobs and not the bookkeeping.
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
//  place. These six cover what scripts in this project actually tune: a
//  distance, a count, a switch, a label, a position and a colour.
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
};

// "float", "colour" - for error messages and the Inspector's tooltips.
const char* ScriptFieldTypeName(ScriptFieldType type);

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
                      "std::string, eng::Vec2, eng::Color. Those are the ones the "
                      "Inspector can draw and the scene file can store. For "
                      "anything else, expose the parts of it you want to tune - a "
                      "list of waypoints is not something a property grid can "
                      "edit, and pretending otherwise would be worse than leaving "
                      "it out.");

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
        static_assert(fields::Traits<Clean>::known,
                      "a script variable must be one of: float, int, bool, "
                      "std::string, eng::Vec2, eng::Color.");
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

    std::vector<ScriptField> m_fields;
};

// ---------------------------------------------------------------------------
//  Moving one value between a field and JSON.
//
//  The shapes match what the rest of the engine already uses, so a scene file
//  stays consistent and hand-editable: a Vec2 is [x, y] as ReadVec2 expects,
//  and a Color is [r, g, b, a] from 0 to 255 as SpriteComponent's tint is.
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
