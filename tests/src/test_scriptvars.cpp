// ============================================================================
//  test_scriptvars.cpp - reading a script's variables out of its source text.
//
//  This is a PARSER, and a parser's tests are mostly about what it refuses.
//  Finding `float radius = 90.0f;` is the easy half; the half that decides
//  whether the feature is usable is everything it must leave alone:
//
//    * the words ENGINE_REGISTER_SCRIPT inside the banner comment that every
//      script generated from the template carries;
//    * private members, and the `public:` / `private:` switching above them;
//    * constants, which cannot be edited and whose presence is the classic
//      mistake;
//    * the insides of member functions, which are full of assignments that
//      look like declarations;
//    * types it cannot draw - reported, so nothing is silently absent.
//
//  Raw strings are used for the sources so they read as the C++ they are.
// ============================================================================

#include <doctest/doctest.h>

#include <engine/Engine.h>

#include <algorithm>
#include <string>

using namespace eng;

namespace {

// Did the scan find a variable with this name, and if so which one?
const ScriptVarDecl* Var(const ScriptSourceInfo& info, std::string_view name) {
    const auto it = std::find_if(info.vars.begin(), info.vars.end(),
                                 [name](const ScriptVarDecl& v) { return v.name == name; });
    return it != info.vars.end() ? &*it : nullptr;
}

const ScriptVarSkipped* Skipped(const ScriptSourceInfo& info, std::string_view name) {
    const auto it =
        std::find_if(info.skipped.begin(), info.skipped.end(),
                     [name](const ScriptVarSkipped& s) { return s.name == name; });
    return it != info.skipped.end() ? &*it : nullptr;
}

// A script with one public section, for the many cases that only care about
// what happens to a single declaration.
std::string Wrap(std::string_view members) {
    std::string out = "#include <engine/Engine.h>\nnamespace {\n";
    out += "class Thing final : public eng::ScriptBehaviour {\npublic:\n";
    out += members;
    out += "\n    void OnFixedUpdate() {}\n};\n}\nENGINE_REGISTER_SCRIPT(Thing)\n";
    return out;
}

} // namespace

// ===========================================================================
//  Finding the script at all
// ===========================================================================

TEST_CASE("a file that registers nothing is not a script") {
    const ScriptSourceInfo info = ScanScriptSource(R"CPP(
        #include <engine/Engine.h>
        // A helper, not a script.
        inline float Square(float x) { return x * x; }
    )CPP");

    CHECK(info.className.empty());
    CHECK(info.vars.empty());
}

TEST_CASE("the macro named ONLY IN A COMMENT does not count") {
    // ======================================================================
    //  THE CASE THAT WOULD HAVE BROKEN EVERY SCRIPT IN THE PROJECT.
    //
    //  Every script written from the template carries a banner explaining
    //  that ENGINE_REGISTER_SCRIPT is required, and TestScript.cpp mentions
    //  it twice. Reading those as the real registration would pick up the
    //  wrong class name - or "YourClassName" - and then find no class.
    // ======================================================================
    const ScriptSourceInfo info = ScanScriptSource(R"CPP(
        // Without ENGINE_REGISTER_SCRIPT(YourClassName) at the bottom of the
        // file, the script can never be found.
        /* ENGINE_REGISTER_SCRIPT(AlsoNotReal) */
        #include <engine/Engine.h>
        inline float Square(float x) { return x * x; }
    )CPP");

    CHECK(info.className.empty());
}

TEST_CASE("the class name comes from the registration") {
    const ScriptSourceInfo info = ScanScriptSource(Wrap("    float speed = 1.0f;"));

    CHECK(info.className == "Thing");
    CHECK(info.vars.size() == 1);
}

TEST_CASE("a forward declaration is stepped over to reach the real class") {
    const ScriptSourceInfo info = ScanScriptSource(R"CPP(
        #include <engine/Engine.h>
        namespace {
        class Thing;                        // declared here, defined below
        class Thing final : public eng::ScriptBehaviour {
        public:
            float speed = 2.0f;
            void OnFixedUpdate() {}
        };
        }
        ENGINE_REGISTER_SCRIPT(Thing)
    )CPP");

    REQUIRE(info.className == "Thing");
    REQUIRE(info.vars.size() == 1);
    CHECK(info.vars[0].name == "speed");
}

TEST_CASE("a script whose class cannot be found exposes nothing and does not crash") {
    // A class built by a macro, say. Nothing is exposed automatically, which
    // is exactly the behaviour before any of this existed - and OnExpose still
    // works for it.
    const ScriptSourceInfo info = ScanScriptSource(R"CPP(
        #include <engine/Engine.h>
        MY_SCRIPT_MACRO(Thing, float speed = 1.0f;)
        ENGINE_REGISTER_SCRIPT(Thing)
    )CPP");

    CHECK(info.className == "Thing");
    CHECK(info.vars.empty());
}

// ===========================================================================
//  Access: public is exposed, nothing else is
// ===========================================================================

TEST_CASE("a class starts PRIVATE, so members above the first public: are hidden") {
    // The one place `class` and `struct` differ here, and getting it backwards
    // would put every script's private bookkeeping on screen.
    const ScriptSourceInfo info = ScanScriptSource(R"CPP(
        #include <engine/Engine.h>
        namespace {
        class Thing final : public eng::ScriptBehaviour {
            float hidden = 1.0f;
        public:
            float shown = 2.0f;
            void OnFixedUpdate() {}
        };
        }
        ENGINE_REGISTER_SCRIPT(Thing)
    )CPP");

    CHECK(Var(info, "shown") != nullptr);
    CHECK(Var(info, "hidden") == nullptr);
}

TEST_CASE("a struct starts PUBLIC") {
    const ScriptSourceInfo info = ScanScriptSource(R"CPP(
        #include <engine/Engine.h>
        namespace {
        struct Thing : eng::ScriptBehaviour {
            float shown = 2.0f;
            void OnFixedUpdate() {}
        };
        }
        ENGINE_REGISTER_SCRIPT(Thing)
    )CPP");

    CHECK(Var(info, "shown") != nullptr);
}

TEST_CASE("switching back to private hides what follows") {
    const ScriptSourceInfo info = ScanScriptSource(R"CPP(
        #include <engine/Engine.h>
        namespace {
        class Thing final : public eng::ScriptBehaviour {
        public:
            float first = 1.0f;
            void OnFixedUpdate() {}
        private:
            float second = 2.0f;
        protected:
            float third = 3.0f;
        public:
            float fourth = 4.0f;
        };
        }
        ENGINE_REGISTER_SCRIPT(Thing)
    )CPP");

    CHECK(Var(info, "first") != nullptr);
    CHECK(Var(info, "second") == nullptr);
    CHECK(Var(info, "third") == nullptr);
    CHECK(Var(info, "fourth") != nullptr);
}

// ===========================================================================
//  What is left alone
// ===========================================================================

TEST_CASE("the inside of a member function is not read for declarations") {
    // A function body is full of lines that look like declarations. Reading
    // them would expose locals that do not exist outside the call.
    const ScriptSourceInfo info = ScanScriptSource(Wrap(R"CPP(
    float speed = 1.0f;

    void OnStart() {
        float local = 5.0f;
        int   count = 2;
        bool  flag  = true;
    }
)CPP"));

    CHECK(Var(info, "speed") != nullptr);
    CHECK(Var(info, "local") == nullptr);
    CHECK(Var(info, "count") == nullptr);
    CHECK(Var(info, "flag") == nullptr);
}

TEST_CASE("a nested type's members are not the script's members") {
    const ScriptSourceInfo info = ScanScriptSource(Wrap(R"CPP(
    struct Inner { float buried = 1.0f; };
    float speed = 2.0f;
)CPP"));

    CHECK(Var(info, "speed") != nullptr);
    CHECK(Var(info, "buried") == nullptr);
}

TEST_CASE("a member function is not reported as a skipped variable") {
    // It was never a candidate, and reporting every function would bury the
    // reports that matter.
    const ScriptSourceInfo info = ScanScriptSource(Wrap(R"CPP(
    float Speed() const { return 1.0f; }
    void  Reset();
)CPP"));

    CHECK(info.vars.empty());
    CHECK(info.skipped.empty());
}

TEST_CASE("a declaration inside a block comment is invisible") {
    const ScriptSourceInfo info = ScanScriptSource(Wrap(R"CPP(
    /*
    float commented = 1.0f;
    */
    float real = 2.0f;
)CPP"));

    CHECK(Var(info, "real") != nullptr);
    CHECK(Var(info, "commented") == nullptr);
}

TEST_CASE("braces inside a string literal do not confuse the depth counting") {
    const ScriptSourceInfo info = ScanScriptSource(Wrap(R"CPP(
    std::string opener = "{{{";
    float after = 3.0f;
)CPP"));

    CHECK(Var(info, "opener") != nullptr);
    CHECK(Var(info, "after") != nullptr);
}

// ===========================================================================
//  What is refused, and reported
// ===========================================================================

TEST_CASE("a constant is skipped, and the reason says what to do about it") {
    // The classic mistake: `static constexpr float kRadius = 90.0f` is what
    // these scripts used before they were tunable, and it cannot be edited.
    const ScriptSourceInfo info = ScanScriptSource(Wrap(R"CPP(
    static constexpr float kRadius = 90.0f;
    const float fixed = 1.0f;
    float tunable = 2.0f;
)CPP"));

    CHECK(Var(info, "kRadius") == nullptr);
    CHECK(Var(info, "fixed") == nullptr);
    CHECK(Var(info, "tunable") != nullptr);

    REQUIRE(Skipped(info, "kRadius") != nullptr);
    CHECK(Skipped(info, "kRadius")->reason.find("ordinary member") != std::string::npos);
    CHECK(Skipped(info, "fixed") != nullptr);
}

TEST_CASE("a pointer or a reference is skipped") {
    const ScriptSourceInfo info = ScanScriptSource(Wrap(R"CPP(
    float* aimed = nullptr;
    float& bound = *aimed;
    float value = 1.0f;
)CPP"));

    CHECK(Var(info, "aimed") == nullptr);
    CHECK(Var(info, "bound") == nullptr);
    CHECK(Var(info, "value") != nullptr);

    REQUIRE(Skipped(info, "aimed") != nullptr);
    CHECK(Skipped(info, "aimed")->reason.find("pointer") != std::string::npos);
    REQUIRE(Skipped(info, "bound") != nullptr);
    CHECK(Skipped(info, "bound")->reason.find("reference") != std::string::npos);
}

TEST_CASE("an m_ prefix hides NOTHING - public means shown") {
    // ======================================================================
    //  THIS USED TO BE THE OPPOSITE, and the change is deliberate.
    //
    //  An m_ prefix was read as this engine's [HideInInspector], because the
    //  engine's own code names every internal member that way. It was a trap:
    //  half the C++ world names ORDINARY members m_something, and somebody who
    //  does got no box, no entry in the skipped report, and no way at all to
    //  find out why their variable was missing. A rule that hides things by
    //  spelling is invisible precisely to the people it surprises.
    //
    //  `private:` is the language's own way to say "not part of the
    //  interface", it cannot be arrived at by accident, and it is what the
    //  docs point at now. See the case below.
    // ======================================================================
    const ScriptSourceInfo info = ScanScriptSource(Wrap(R"CPP(
    float m_internal = 1.0f;
    std::string m_name;
    float shown = 2.0f;
)CPP"));

    CHECK(Var(info, "m_internal") != nullptr);
    CHECK(Var(info, "m_name") != nullptr);
    CHECK(Var(info, "shown") != nullptr);

    // Nothing is skipped: all three are variables of drawable types.
    CHECK(info.skipped.empty());
}

TEST_CASE("a public pointer is reported whatever it is called") {
    // The consequence of the change above, and the reason the engine's own
    // scripts moved their pointers into private sections: a PUBLIC one is now
    // named in the skipped report rather than hidden by its prefix. That is
    // the honest behaviour - it says why there is no box - but it is a line on
    // every build, so a pointer belongs in `private:`.
    const ScriptSourceInfo info =
        ScanScriptSource(Wrap("    Ridigbody2D m_body = nullptr;"));

    CHECK(Var(info, "m_body") == nullptr);
    REQUIRE(Skipped(info, "m_body") != nullptr);
}

TEST_CASE("a pointer to an undocumented type is reported as a pointer") {
    // THE BUG THIS PINS. The "it is a pointer" message used to be reachable
    // only for pointers to the eight types the Inspector draws, because it
    // was asked after the type had already matched the table. A component
    // pointer - the commonest pointer a script holds - fell through to the
    // unknown-type branch and was told that its TYPE could not be drawn,
    // with a list of scalars offered instead. The type was never the point.
    //
    // All three spellings mean one thing and must say one thing. The third
    // used to be reported as nothing at all.
    const ScriptSourceInfo info = ScanScriptSource(Wrap(R"CPP(
    SpriteComponent* starRight = nullptr;
    SpriteComponent *starLeft = nullptr;
    ColliderComponent& bound = *starRight;
)CPP"));

    for (const char* name : {"starRight", "starLeft"}) {
        CHECK(Var(info, name) == nullptr);
        REQUIRE(Skipped(info, name) != nullptr);
        CHECK(Skipped(info, name)->reason.find("pointer") != std::string::npos);
        CHECK(Skipped(info, name)->reason.find("not one the Inspector can draw") ==
              std::string::npos);
    }

    REQUIRE(Skipped(info, "bound") != nullptr);
    CHECK(Skipped(info, "bound")->reason.find("reference") != std::string::npos);
}

TEST_CASE("an unknown alias initialised from nullptr is still a pointer") {
    // THIS USED TO SAY `Rigidbody2D m_body = nullptr;`, and that line now means
    // something else: Rigidbody2D is a component REFERENCE and the table
    // recognises it, so it is a field rather than a skip - see the case below.
    //
    // The rule is still needed, because a project may typedef a pointer of its
    // own and this parser cannot know the name. `= nullptr` is what tells it
    // apart from a value, a value being the one thing that cannot be built
    // from one.
    const ScriptSourceInfo info =
        ScanScriptSource(Wrap("    SomeHandleOfMyOwn m_thing = nullptr;"));

    CHECK(Var(info, "m_thing") == nullptr);
    REQUIRE(Skipped(info, "m_thing") != nullptr);
    CHECK(Skipped(info, "m_thing")->reason.find("pointer") != std::string::npos);
    CHECK(Skipped(info, "m_thing")->reason.find("by value") == std::string::npos);
}

TEST_CASE("a component reference is a field, and constrains itself") {
    // The whole point of ComponentRef: the component it is a reference TO
    // rides the C++ type, so the constraint the Inspector filters its picker
    // by cannot drift away from the thing the script actually calls. Nobody
    // writes a `// [SpriteComponent]` marker and nobody can get it wrong.
    const ScriptSourceInfo info = ScanScriptSource(Wrap(R"CPP(
    TransformRef   anchor;
    SpriteRenderer flash;
    Rigidbody2D    pushed;
    eng::ComponentRef<eng::TilemapComponent> ground;
    ComponentRef<BoxColliderComponent> area;
)CPP"));

    struct Expected {
        const char* name;
        const char* required;
    };
    for (const Expected e : {Expected{"anchor", "TransformComponent"},
                             Expected{"flash", "SpriteComponent"},
                             Expected{"pushed", "Rigidbody2DComponent"},
                             Expected{"ground", "TilemapComponent"},
                             Expected{"area", "BoxColliderComponent"}}) {
        REQUIRE(Var(info, e.name) != nullptr);
        CHECK(Var(info, e.name)->fieldType == ScriptFieldType::Entity);
        CHECK(Var(info, e.name)->requiredType == e.required);
    }

    // Every one of them is a variable, so nothing was skipped and nothing was
    // reported - which is the difference the owner asked for.
    CHECK(info.skipped.empty());
}

TEST_CASE("a marker still beats the reference type's own constraint") {
    // Narrowing a Rigidbody2D slot would be odd, but narrowing a collider one
    // is not, and somebody who wrote a marker meant it. The type is the
    // DEFAULT constraint, not an override of what was asked for.
    const ScriptSourceInfo info =
        ScanScriptSource(Wrap("    Rigidbody2D pushed;   // [BoxColliderComponent]"));

    REQUIRE(Var(info, "pushed") != nullptr);
    CHECK(Var(info, "pushed")->requiredType == "BoxColliderComponent");
}

TEST_CASE("a reference to something that is not a component is left alone") {
    // ComponentRef<T> static_asserts on T, but that is a COMPILER's job and
    // this parser never sees a type. An empty argument is not a declaration it
    // can make sense of, so it falls through rather than inventing a field.
    const ScriptSourceInfo info = ScanScriptSource(Wrap("    ComponentRef<> nothing;"));
    CHECK(Var(info, "nothing") == nullptr);
}

TEST_CASE("a component held by value says so, and says what to do") {
    // The declaration that started this: four compiler errors, all of them on
    // the three lines that USED the member and none on the line that declared
    // it. This is the one place that sees the declaration, so it is the only
    // place that can name it.
    const ScriptSourceInfo info =
        ScanScriptSource(Wrap("    SpriteComponent m_sprite;"));

    CHECK(Var(info, "m_sprite") == nullptr);
    REQUIRE(Skipped(info, "m_sprite") != nullptr);
    const std::string& why = Skipped(info, "m_sprite")->reason;
    CHECK(why.find("declares one by value") != std::string::npos);
    CHECK(why.find("Find<eng::SpriteComponent>()") != std::string::npos);

    // And the list it offers instead is the list the table really handles.
    CHECK(why.find("ImageRef") != std::string::npos);
    CHECK(why.find("AnimationRef") != std::string::npos);
}

TEST_CASE("a name that merely contains m_ is not hidden") {
    const ScriptSourceInfo info = ScanScriptSource(Wrap(R"CPP(
    float warm_up = 1.0f;
    float m = 2.0f;
)CPP"));

    CHECK(Var(info, "warm_up") != nullptr);
    CHECK(Var(info, "m") != nullptr);
}

TEST_CASE("a type the Inspector cannot draw is skipped BY NAME") {
    // Reported rather than ignored. "Why is my variable not there" has to be
    // answerable, and this is the answer for the commonest reason.
    const ScriptSourceInfo info = ScanScriptSource(Wrap(R"CPP(
    double precise = 1.0;
    float supported = 2.0f;
)CPP"));

    CHECK(Var(info, "precise") == nullptr);
    REQUIRE(Skipped(info, "precise") != nullptr);
    CHECK(Skipped(info, "precise")->reason.find("double") != std::string::npos);
    CHECK(Var(info, "supported") != nullptr);
}

TEST_CASE("two variables sharing a line are skipped, with the fix in the reason") {
    const ScriptSourceInfo info = ScanScriptSource(Wrap("    float a = 1.0f, b = 2.0f;"));

    CHECK(info.vars.empty());
    REQUIRE(info.skipped.size() == 1);
    CHECK(info.skipped[0].name == "a");
    CHECK(info.skipped[0].reason.find("line of its own") != std::string::npos);
}

// ===========================================================================
//  Types
// ===========================================================================

TEST_CASE("every supported type is recognised, however it is spelled") {
    // Three spellings of Vec2 all mean the same thing - eng::Vec2, Vec2 after
    // `using namespace eng`, and Vector2 from the typedef in Engine.h. A
    // spelling that is not recognised is a variable that silently vanishes.
    const ScriptSourceInfo info = ScanScriptSource(Wrap(R"CPP(
    float          a = 1.0f;
    int            b = 2;
    bool           c = true;
    std::string    d = "x";
    eng::Vec2      e;
    Vec2           f;
    Vector2        g;
    eng::Color     h;
    Color          i;
    eng::EntityRef j;
    EntityRef      k;
    eng::PrefabRef l;
    PrefabRef      m;
    eng::AnimationRef n;
    AnimationRef      o;
    eng::ImageRef     p;
    ImageRef          q;
)CPP"));

    REQUIRE(info.vars.size() == 17);
    CHECK(Var(info, "a")->fieldType == ScriptFieldType::Float);
    CHECK(Var(info, "b")->fieldType == ScriptFieldType::Int);
    CHECK(Var(info, "c")->fieldType == ScriptFieldType::Bool);
    CHECK(Var(info, "d")->fieldType == ScriptFieldType::String);
    CHECK(Var(info, "e")->fieldType == ScriptFieldType::Vec2);
    CHECK(Var(info, "f")->fieldType == ScriptFieldType::Vec2);
    CHECK(Var(info, "g")->fieldType == ScriptFieldType::Vec2);
    CHECK(Var(info, "h")->fieldType == ScriptFieldType::Color);
    CHECK(Var(info, "i")->fieldType == ScriptFieldType::Color);
    CHECK(Var(info, "j")->fieldType == ScriptFieldType::Entity);
    CHECK(Var(info, "k")->fieldType == ScriptFieldType::Entity);

    // The two reference types that were never in this list. A spelling the
    // whitelist does not know is a variable that silently vanishes, so the
    // list being incomplete is the one thing this case exists to prevent.
    CHECK(Var(info, "l")->fieldType == ScriptFieldType::Prefab);
    CHECK(Var(info, "m")->fieldType == ScriptFieldType::Prefab);
    CHECK(Var(info, "n")->fieldType == ScriptFieldType::Animation);
    CHECK(Var(info, "o")->fieldType == ScriptFieldType::Animation);
    CHECK(Var(info, "p")->fieldType == ScriptFieldType::Image);
    CHECK(Var(info, "q")->fieldType == ScriptFieldType::Image);
}

TEST_CASE("an array of AnimationRef is exposed, one slot per element") {
    const ScriptSourceInfo info =
        ScanScriptSource(Wrap("    eng::AnimationRef walkAnimations[4];"));

    const ScriptVarDecl* var = Var(info, "walkAnimations");
    REQUIRE(var != nullptr);
    CHECK(var->fieldType == ScriptFieldType::Animation);
    CHECK(var->isArray);

    // Four ordinary variables with four names, which is what makes the type
    // need to know nothing about arrays. See ScriptVars.h.
    const std::string unit = GenerateExposerUnit(info, "scripts/T.cpp");
    CHECK(unit.find("::std::size(s->walkAnimations)") != std::string::npos);
    CHECK(unit.find("f.Var(") != std::string::npos);
}

TEST_CASE("a leftover [animation] marker on an AnimationRef is dropped, not carried") {
    // ======================================================================
    //  THE FILTER THAT MAKES THE OLD SPELLING SAFE TO LEAVE BEHIND.
    //
    //  Somebody changing `std::string x; // [animation]` to
    //  `eng::AnimationRef x; // [animation]` will not always delete the
    //  comment, and they should not have to. requiredType is cleared for
    //  every type that is neither Entity nor String, so the marker becomes
    //  ordinary prose and the tooltip keeps the rest of the line.
    //
    //  WITHOUT THAT FILTER THE FIELD WOULD VANISH. The generator chooses Ref
    //  when a requiredType survives on a non-string, so it would emit
    //  f.Ref(name, AnimationRef&, "animation") - ill-formed, swallowed by the
    //  `if constexpr (requires ...)` guard around every generated call, and
    //  therefore silent. A clean build, a Console line saying the variable
    //  was exposed, and nothing in the Inspector.
    // ======================================================================
    const ScriptSourceInfo info = ScanScriptSource(
        Wrap("    eng::AnimationRef walkLeft;   // [animation] which clip to play"));

    const ScriptVarDecl* var = Var(info, "walkLeft");
    REQUIRE(var != nullptr);
    CHECK(var->fieldType == ScriptFieldType::Animation);
    CHECK(var->requiredType.empty());
    CHECK(var->tooltip == "which clip to play");

    const std::string unit = GenerateExposerUnit(info, "scripts/T.cpp");
    CHECK(unit.find("f.Var(\"walkLeft\"") != std::string::npos);
    CHECK(unit.find("f.Ref(") == std::string::npos);
    CHECK(unit.find("f.Asset(") == std::string::npos);
}

TEST_CASE("an array of ImageRef is exposed, one slot per element") {
    const ScriptSourceInfo info =
        ScanScriptSource(Wrap("    eng::ImageRef faces[4];"));

    const ScriptVarDecl* var = Var(info, "faces");
    REQUIRE(var != nullptr);
    CHECK(var->fieldType == ScriptFieldType::Image);
    CHECK(var->isArray);

    const std::string unit = GenerateExposerUnit(info, "scripts/T.cpp");
    CHECK(unit.find("::std::size(s->faces)") != std::string::npos);
    CHECK(unit.find("f.Var(") != std::string::npos);
}

TEST_CASE("a leftover [image] marker on an ImageRef is dropped, not carried") {
    // The same filter that makes a leftover [animation] safe, and the same
    // silent failure without it: a requiredType surviving on a non-string
    // makes the generator choose Ref, so it would emit
    // f.Ref(name, ImageRef&, "image") - ill-formed, swallowed by the
    // `if constexpr (requires ...)` guard around every generated call, and
    // therefore invisible. A clean build and nothing in the Inspector.
    const ScriptSourceInfo info = ScanScriptSource(
        Wrap("    eng::ImageRef icon;   // [image] shown when carried"));

    const ScriptVarDecl* var = Var(info, "icon");
    REQUIRE(var != nullptr);
    CHECK(var->fieldType == ScriptFieldType::Image);
    CHECK(var->requiredType.empty());
    CHECK(var->tooltip == "shown when carried");

    const std::string unit = GenerateExposerUnit(info, "scripts/T.cpp");
    CHECK(unit.find("f.Var(\"icon\"") != std::string::npos);
    CHECK(unit.find("f.Ref(") == std::string::npos);
    CHECK(unit.find("f.Asset(") == std::string::npos);
}

TEST_CASE("a variable with no initialiser is still a variable") {
    const ScriptSourceInfo info = ScanScriptSource(Wrap("    eng::EntityRef target;"));

    REQUIRE(info.vars.size() == 1);
    CHECK(info.vars[0].name == "target");
}

TEST_CASE("a brace initialiser is a variable too") {
    const ScriptSourceInfo info = ScanScriptSource(Wrap(R"CPP(
    eng::Vec2 offset{3.0f, -4.0f};
    eng::EntityRef preset{"HudScore"};
)CPP"));

    CHECK(Var(info, "offset") != nullptr);
    CHECK(Var(info, "preset") != nullptr);
}

TEST_CASE("declaration order is kept") {
    const ScriptSourceInfo info = ScanScriptSource(Wrap(R"CPP(
    float third = 3.0f;
    float first = 1.0f;
    float second = 2.0f;
)CPP"));

    REQUIRE(info.vars.size() == 3);
    CHECK(info.vars[0].name == "third");
    CHECK(info.vars[1].name == "first");
    CHECK(info.vars[2].name == "second");
}

// ===========================================================================
//  The trailing comment
// ===========================================================================

TEST_CASE("a trailing comment becomes the tooltip") {
    const ScriptSourceInfo info =
        ScanScriptSource(Wrap("    float speed = 1.0f;   // how fast it goes"));

    REQUIRE(info.vars.size() == 1);
    CHECK(info.vars[0].tooltip == "how fast it goes");
}

TEST_CASE("a bracket group at the start is a marker, and the rest is the tooltip") {
    const ScriptSourceInfo info =
        ScanScriptSource(Wrap("    float blend = 0.5f;   // [0..1] how much to mix"));

    REQUIRE(info.vars.size() == 1);
    CHECK(info.vars[0].ranged);
    CHECK(info.vars[0].min == doctest::Approx(0.0f));
    CHECK(info.vars[0].max == doctest::Approx(1.0f));
    CHECK(info.vars[0].tooltip == "how much to mix");
}

TEST_CASE("a bracket group NOT at the start is ordinary prose") {
    // A comment is documentation first. Treating "[1]" anywhere in it as a
    // marker would turn a reference into a footnote.
    const ScriptSourceInfo info =
        ScanScriptSource(Wrap("    float speed = 1.0f;   // see [1] for why"));

    REQUIRE(info.vars.size() == 1);
    CHECK_FALSE(info.vars[0].ranged);
    CHECK(info.vars[0].requiredType.empty());
    CHECK(info.vars[0].tooltip == "see [1] for why");
}

TEST_CASE("a marker on a reference is the type the target must carry") {
    const ScriptSourceInfo info = ScanScriptSource(
        Wrap("    eng::EntityRef body;   // [Rigidbody2DComponent] what to push"));

    REQUIRE(info.vars.size() == 1);
    CHECK(info.vars[0].requiredType == "Rigidbody2DComponent");
    CHECK(info.vars[0].tooltip == "what to push");
}

TEST_CASE("an int range keeps whole numbers") {
    const ScriptSourceInfo info =
        ScanScriptSource(Wrap("    int lives = 3;   // [1..9] how many tries"));

    REQUIRE(info.vars.size() == 1);
    CHECK(info.vars[0].ranged);
    CHECK(info.vars[0].min == doctest::Approx(1.0f));
    CHECK(info.vars[0].max == doctest::Approx(9.0f));
}

TEST_CASE("a marker that does not apply to the type is dropped") {
    // A range on a string and a required type on a float mean nothing, so
    // nothing downstream has to ask whether they apply.
    const ScriptSourceInfo info = ScanScriptSource(Wrap(R"CPP(
    std::string label = "x";   // [0..1] not a number
    float speed = 1.0f;        // [Rigidbody2DComponent] not a reference
)CPP"));

    REQUIRE(Var(info, "label") != nullptr);
    CHECK_FALSE(Var(info, "label")->ranged);
    REQUIRE(Var(info, "speed") != nullptr);
    CHECK(Var(info, "speed")->requiredType.empty());
}

TEST_CASE("a range written backwards is ignored rather than obeyed") {
    const ScriptSourceInfo info =
        ScanScriptSource(Wrap("    float speed = 1.0f;   // [10..0] backwards"));

    REQUIRE(info.vars.size() == 1);
    CHECK_FALSE(info.vars[0].ranged);
}

TEST_CASE("a malformed marker leaves the variable alone") {
    const ScriptSourceInfo info = ScanScriptSource(Wrap(R"CPP(
    float a = 1.0f;   // [..] nothing in it
    float b = 2.0f;   // [x..y] not numbers
    float c = 3.0f;   // [unclosed
)CPP"));

    CHECK(info.vars.size() == 3);
    CHECK_FALSE(Var(info, "a")->ranged);
    CHECK_FALSE(Var(info, "b")->ranged);
    CHECK_FALSE(Var(info, "c")->ranged);
}

// ===========================================================================
//  A hand-written OnExpose wins
// ===========================================================================

TEST_CASE("OnExpose is noticed") {
    const ScriptSourceInfo info = ScanScriptSource(Wrap(R"CPP(
    float speed = 1.0f;
    void OnExpose(eng::ScriptFields& fields) { fields.Var("speed", speed); }
)CPP"));

    CHECK(info.hasOnExpose);
}

TEST_CASE("a script without OnExpose says so") {
    const ScriptSourceInfo info = ScanScriptSource(Wrap("    float speed = 1.0f;"));
    CHECK_FALSE(info.hasOnExpose);
}

// ===========================================================================
//  The generated translation unit
// ===========================================================================

TEST_CASE("the generated unit includes the original rather than copying it") {
    // THE WHOLE TRICK. Including it keeps every line number, every __FILE__
    // and the file's own static helpers intact, so a compiler error still
    // points at the line somebody wrote.
    ScriptSourceInfo info;
    info.className = "Orbiter";
    info.vars.push_back({"radius", "float", ScriptFieldType::Float, "", false, 0, 0, ""});

    const std::string unit = GenerateExposerUnit(info, "C:/x/Orbiter.cpp");

    CHECK(unit.find("#include \"C:/x/Orbiter.cpp\"") != std::string::npos);
    CHECK(unit.find("SetExposer(\"Orbiter\"") != std::string::npos);
    CHECK(unit.find("f.Var(\"radius\", s->radius)") != std::string::npos);
}

TEST_CASE("every variable is guarded so generated code cannot break the build") {
    // A variable this parser imagined, or one that turned out to be
    // inaccessible, has to COMPILE AWAY rather than fail. `requires` only
    // does that inside a template, which is why the generated code wraps each
    // one in a generic lambda - the same rule ScriptHooks.h documents.
    ScriptSourceInfo info;
    info.className = "Thing";
    info.vars.push_back({"a", "float", ScriptFieldType::Float, "", false, 0, 0, ""});
    info.vars.push_back({"b", "int", ScriptFieldType::Int, "", false, 0, 0, ""});

    const std::string unit = GenerateExposerUnit(info, "x.cpp");

    CHECK(unit.find("[]<class S>(S* s") != std::string::npos);
    const std::size_t guards =
        [&unit] {
            std::size_t count = 0;
            for (std::size_t at = unit.find("if constexpr (requires");
                 at != std::string::npos;
                 at = unit.find("if constexpr (requires", at + 1)) {
                ++count;
            }
            return count;
        }();
    CHECK(guards == 2);
}

TEST_CASE("a range's bounds are written in the VARIABLE'S OWN TYPE") {
    // ======================================================================
    //  Load-bearing, and silent if it is wrong.
    //
    //  ScriptFields::Range takes (T&, T, T). Hand an int variable a 0.0f
    //  bound and the deduction is ambiguous, the requires guard is false, and
    //  the slider is quietly absent - no error anywhere.
    // ======================================================================
    ScriptSourceInfo info;
    info.className = "Thing";
    info.vars.push_back({"lives", "int", ScriptFieldType::Int, "", true, 1.0f, 9.0f, ""});
    info.vars.push_back({"blend", "float", ScriptFieldType::Float, "", true, 0.0f, 1.0f, ""});

    const std::string unit = GenerateExposerUnit(info, "x.cpp");

    CHECK(unit.find("f.Range(\"lives\", s->lives, 1, 9)") != std::string::npos);
    CHECK(unit.find("f.Range(\"blend\", s->blend, 0.000000f, 1.000000f)") !=
          std::string::npos);
}

TEST_CASE("a reference's required type reaches the generated Ref call") {
    ScriptSourceInfo info;
    info.className = "Thing";
    info.vars.push_back({"body", "eng::EntityRef", ScriptFieldType::Entity, "push it",
                         false, 0, 0, "Rigidbody2DComponent"});

    const std::string unit = GenerateExposerUnit(info, "x.cpp");

    CHECK(unit.find("f.Ref(\"body\", s->body, \"Rigidbody2DComponent\", \"push it\")") !=
          std::string::npos);
}

TEST_CASE("a tooltip with a quote in it does not break the generated source") {
    ScriptSourceInfo info;
    info.className = "Thing";
    info.vars.push_back({"speed", "float", ScriptFieldType::Float,
                         "the \"real\" speed \\ here", false, 0, 0, ""});

    const std::string unit = GenerateExposerUnit(info, "x.cpp");

    CHECK(unit.find("\\\"real\\\"") != std::string::npos);
    CHECK(unit.find("\\\\ here") != std::string::npos);
}

TEST_CASE("a script with no variables still generates a unit that compiles") {
    // It is never written in that case - see ScriptBuild - but the generator
    // producing something degenerate rather than something broken is worth
    // knowing.
    ScriptSourceInfo info;
    info.className = "Thing";

    const std::string unit = GenerateExposerUnit(info, "x.cpp");
    CHECK(unit.find("(void)self; (void)fields;") != std::string::npos);
    CHECK(unit.find("if constexpr") == std::string::npos);
}

TEST_CASE("a member named like one of ScriptBehaviour's own is skipped, not exposed") {
    // ======================================================================
    //  THE FIVE NAMES A SCRIPT DOES NOT OWN - transform, gameObject, scene,
    //  name and tag - and the case whose absence let a build-breaker through.
    //
    //  Two different mistakes arrive at the parser as the same declaration.
    //  A real member of the user's own HIDES the inherited handle, so exposing
    //  it would put a row labelled "tag" in the Inspector with nothing to do
    //  with the entity's tag. And a variable this parser only IMAGINED - here
    //  one inside an `#if 0`, which the whitelist does not read - used to be
    //  discarded by the `requires` guard in the generated code, because no
    //  member of that name existed. Against these five it resolves to the
    //  handle, the guard is satisfied, and ScriptFields::Var's own
    //  static_assert then fails the user's build from a generated file.
    //
    //  Either way the answer is the same: not in `vars`, and in `skipped`
    //  with a reason, which is what the Console prints.
    // ======================================================================
    const ScriptSourceInfo info = ScanScriptSource(R"CPP(
#include <engine/Engine.h>

class Thing final : public eng::ScriptBehaviour {
public:
    float speed = 1.0f;         // this one really is the script's
    std::string tag;            // hides eng::ScriptBehaviour::tag
#if 0
    std::string name;           // imagined: the parser does not read #if
#endif

    void OnFixedUpdate() {}
};

ENGINE_REGISTER_SCRIPT(Thing)
)CPP");

    CHECK(info.className == "Thing");

    REQUIRE(info.vars.size() == 1);
    CHECK(info.vars[0].name == "speed");

    REQUIRE(info.skipped.size() == 2);
    CHECK(info.skipped[0].name == "tag");
    CHECK(info.skipped[1].name == "name");
    for (const ScriptVarSkipped& one : info.skipped) {
        CHECK(one.reason.find("already has a member of that name") !=
              std::string::npos);
    }
}

// ===========================================================================
//  The project's own scripts, as they are actually written
// ===========================================================================

TEST_CASE("a realistic script is read the way it is written") {
    // Shaped like assets/scripts/Orbiter.cpp: a banner mentioning the macro, a
    // using-declaration, public tunables with trailing comments, private
    // bookkeeping, and member functions in between.
    const ScriptSourceInfo info = ScanScriptSource(R"CPP(
// ============================================================================
//  Orbiter - a worked example.
//
//  Registers with ENGINE_REGISTER_SCRIPT(Orbiter) at the bottom.
// ============================================================================

#include <engine/Engine.h>
#include <cmath>

namespace {

class Orbiter final : public eng::ScriptBehaviour {
public:
    float radius = 90.0f;                // how far out it circles, in pixels
    float degreesPerSecond = 68.75f;     // [0..360] how fast it goes round
    eng::EntityRef centre;               // [TransformComponent] what to circle

    void OnStart() {
        if (transform != nullptr) {
            m_start = transform->LocalPosition();
        }
    }

    void OnFixedUpdate() {
        float local = radius * 2.0f;
        m_angle += degreesPerSecond * eng::kDegToRad * eng::Time::FixedDeltaTime();
        if (m_angle > eng::kTwoPi) { m_angle -= eng::kTwoPi; }
    }

private:
    eng::Vec2 m_start{};
    float     m_angle = 0.0f;
};

} // namespace

ENGINE_REGISTER_SCRIPT(Orbiter)
)CPP");

    CHECK(info.className == "Orbiter");
    CHECK_FALSE(info.hasOnExpose);

    REQUIRE(info.vars.size() == 3);
    CHECK(info.vars[0].name == "radius");
    CHECK(info.vars[0].tooltip == "how far out it circles, in pixels");
    CHECK(info.vars[1].name == "degreesPerSecond");
    CHECK(info.vars[1].ranged);
    CHECK(info.vars[1].max == doctest::Approx(360.0f));
    CHECK(info.vars[2].name == "centre");
    CHECK(info.vars[2].requiredType == "TransformComponent");

    // The private bookkeeping and the function local are all left alone.
    CHECK(Var(info, "m_start") == nullptr);
    CHECK(Var(info, "m_angle") == nullptr);
    CHECK(Var(info, "local") == nullptr);
}

// ===========================================================================
//  Fixed-size arrays - one row per element
//
//  A public array is what puts several boxes in the Inspector under one name:
//  "walkAnimations[0]" to "walkAnimations[3]". The scan says it IS an array
//  and never reads the length, because the generated exposer loops over
//  std::size() - so the declaration may say [4], or [kFacingCount], or
//  anything else the compiler can count. See ScriptVarDecl::isArray.
// ===========================================================================

TEST_CASE("an array member is exposed, and says it is one") {
    const ScriptSourceInfo info =
        ScanScriptSource(Wrap("    std::string walkAnimations[4];"));

    const ScriptVarDecl* var = Var(info, "walkAnimations");
    REQUIRE(var != nullptr);
    CHECK(var->isArray);
    CHECK(var->fieldType == ScriptFieldType::String);

    // THE NAME HAS NO BRACKETS ON IT. The brackets belong to the declarator,
    // and the element names are built by the generated loop - so what the scan
    // reports is the member, once.
    CHECK(var->name == "walkAnimations");
}

TEST_CASE("the length is never read, so it may be a named constant") {
    // THE POINT OF LOOPING OVER std::size(). A scanner that parsed "[4]" would
    // have to evaluate "[kFacingCount]" as well, which means knowing what the
    // script's constants are - a C++ compiler's job, and one it is about to do
    // anyway.
    const ScriptSourceInfo info =
        ScanScriptSource(Wrap("    std::string byDirection[kFacingCount];"));

    const ScriptVarDecl* var = Var(info, "byDirection");
    REQUIRE(var != nullptr);
    CHECK(var->isArray);
}

TEST_CASE("an array of every drawable type is exposed") {
    const ScriptSourceInfo info = ScanScriptSource(Wrap(
        "    float    weights[3];\n"
        "    int      counts[2];\n"
        "    bool     flags[2];\n"
        "    Vector2  offsets[4];\n"
        "    Color    palette[5];\n"));

    REQUIRE(info.vars.size() == 5);
    for (const ScriptVarDecl& var : info.vars) {
        CHECK(var.isArray);
    }
    CHECK(Var(info, "weights")->fieldType == ScriptFieldType::Float);
    CHECK(Var(info, "offsets")->fieldType == ScriptFieldType::Vec2);
    CHECK(Var(info, "palette")->fieldType == ScriptFieldType::Color);
}

TEST_CASE("an initialised array is still one variable, not two") {
    // THE COMMA INSIDE THE BRACES IS NOT A SECOND DECLARATOR, which is the
    // same rule a Vec2's initialiser already relies on. Without it this would
    // be reported as "it shares its line with another variable".
    const ScriptSourceInfo info = ScanScriptSource(
        Wrap("    std::string names[2] = {\"left\", \"right\"};"));

    REQUIRE(info.vars.size() == 1);
    CHECK(info.vars[0].name == "names");
    CHECK(info.vars[0].isArray);
    CHECK(Skipped(info, "names") == nullptr);
}

TEST_CASE("a trailing comment on an array is its tooltip, as on anything else") {
    const ScriptSourceInfo info = ScanScriptSource(
        Wrap("    std::string walkAnimations[4];   // right, left, up, down"));

    const ScriptVarDecl* var = Var(info, "walkAnimations");
    REQUIRE(var != nullptr);
    CHECK(var->tooltip == "right, left, up, down");
}

TEST_CASE("a two-dimensional array is left alone rather than half-understood") {
    const ScriptSourceInfo info = ScanScriptSource(Wrap("    float grid[2][2];"));

    CHECK(Var(info, "grid") == nullptr);
}

TEST_CASE("an ordinary member still reports itself as not an array") {
    const ScriptSourceInfo info = ScanScriptSource(Wrap("    float speed = 1.0f;"));

    const ScriptVarDecl* var = Var(info, "speed");
    REQUIRE(var != nullptr);
    CHECK_FALSE(var->isArray);
}

TEST_CASE("the generated exposer loops over an array and names each element") {
    const ScriptSourceInfo info =
        ScanScriptSource(Wrap("    std::string walkAnimations[4];   // by direction"));
    const std::string unit = GenerateExposerUnit(info, "scripts/Thing.cpp");

    // THE LOOP, over std::size rather than a number this file had to work out.
    CHECK(unit.find("for (::std::size_t i = 0; i < ::std::size(s->walkAnimations); "
                    "++i)") != std::string::npos);

    // ONE NAME PER ELEMENT, built at run time. ScriptFields::Add copies the
    // name, which is what makes a temporary string safe here.
    CHECK(unit.find("::std::string(\"walkAnimations[\") + ::std::to_string(i) + \"]\"") !=
          std::string::npos);
    CHECK(unit.find("s->walkAnimations[i]") != std::string::npos);

    // The tooltip rides along, as it does for a plain variable.
    CHECK(unit.find("\"by direction\"") != std::string::npos);

    // And the headers the loop needs are named by the generated file rather
    // than relied on through the engine's own.
    CHECK(unit.find("#include <iterator>") != std::string::npos);
    CHECK(unit.find("#include <string>") != std::string::npos);
}

TEST_CASE("a plain variable's exposer has no loop in it") {
    const ScriptSourceInfo info = ScanScriptSource(Wrap("    float speed = 1.0f;"));
    const std::string      unit = GenerateExposerUnit(info, "scripts/Thing.cpp");

    CHECK(unit.find("for (::std::size_t") == std::string::npos);
    CHECK(unit.find("f.Var(\"speed\", s->speed") != std::string::npos);
}

// ===========================================================================
//  The NAME is not consulted
//
//  An `m_` prefix used to hide a public member silently. Half the C++ world
//  names ordinary members that way, so somebody who does got no box, no
//  skipped report and no way to find out why. `private:` is the language's own
//  way of saying "not part of the interface" and is what the docs now point
//  at. These cases pin that down from both ends.
// ===========================================================================

TEST_CASE("underscores anywhere in a name are ordinary") {
    const ScriptSourceInfo info = ScanScriptSource(Wrap(
        "    float walk_speed = 1.0f;\n"
        "    float _leading = 2.0f;\n"
        "    float trailing_ = 3.0f;\n"
        "    float two_words_here = 4.0f;\n"
        "    std::string walk_animations[4];\n"));

    CHECK(Var(info, "walk_speed") != nullptr);
    CHECK(Var(info, "_leading") != nullptr);
    CHECK(Var(info, "trailing_") != nullptr);
    CHECK(Var(info, "two_words_here") != nullptr);
    CHECK(Var(info, "walk_animations") != nullptr);
}

TEST_CASE("a PUBLIC m_ member is shown, because public means shown") {
    // THE REGRESSION. This used to return nothing at all, silently.
    const ScriptSourceInfo info =
        ScanScriptSource(Wrap("    float m_walkSpeed = 260.0f;   // how fast"));

    const ScriptVarDecl* var = Var(info, "m_walkSpeed");
    REQUIRE(var != nullptr);
    CHECK(var->fieldType == ScriptFieldType::Float);
    CHECK(var->tooltip == "how fast");

    // And it is not in the skipped report either - it is simply a variable.
    CHECK(Skipped(info, "m_walkSpeed") == nullptr);
}

TEST_CASE("private is what hides a member, whatever it is called") {
    const ScriptSourceInfo info = ScanScriptSource(
        "#include <engine/Engine.h>\nnamespace {\n"
        "class Thing final : public eng::ScriptBehaviour {\npublic:\n"
        "    float shown = 1.0f;\n"
        "private:\n"
        "    float hidden = 2.0f;\n"
        "    float m_alsoHidden = 3.0f;\n"
        "};\n}\nENGINE_REGISTER_SCRIPT(Thing)\n");

    CHECK(Var(info, "shown") != nullptr);
    CHECK(Var(info, "hidden") == nullptr);
    CHECK(Var(info, "m_alsoHidden") == nullptr);

    // Nothing private is reported as skipped: it was never a candidate.
    CHECK(Skipped(info, "hidden") == nullptr);
}

// ===========================================================================
//  A marker on a string says it holds an asset path
// ===========================================================================

TEST_CASE("a marker survives on a string, so the Inspector can draw a slot") {
    const ScriptSourceInfo info = ScanScriptSource(
        Wrap("    std::string walkAnimations[4];   // [animation] by direction"));

    const ScriptVarDecl* var = Var(info, "walkAnimations");
    REQUIRE(var != nullptr);
    CHECK(var->requiredType == "animation");
    CHECK(var->tooltip == "by direction");
    CHECK(var->isArray);
}

TEST_CASE("a marker on a number is still dropped, because it means nothing there") {
    // [image] on a float is not a required type and not a range. Carrying it
    // would leave something downstream to wonder about.
    const ScriptSourceInfo info =
        ScanScriptSource(Wrap("    float speed = 1.0f;   // [image] nonsense"));

    const ScriptVarDecl* var = Var(info, "speed");
    REQUIRE(var != nullptr);
    CHECK(var->requiredType.empty());
}

TEST_CASE("a range on a string is not a marker it keeps") {
    // "[0..10]" parses as a RANGE, which a string cannot have - so neither the
    // range nor a required type survives, and the tooltip is still right.
    const ScriptSourceInfo info =
        ScanScriptSource(Wrap("    std::string label = \"hi\";   // [0..10] odd"));

    const ScriptVarDecl* var = Var(info, "label");
    REQUIRE(var != nullptr);
    CHECK_FALSE(var->ranged);
    CHECK(var->requiredType.empty());
    CHECK(var->tooltip == "odd");
}

TEST_CASE("a marked string generates Asset, and a marked reference generates Ref") {
    // ======================================================================
    //  THE BUG THIS PINS DOWN WAS SILENT, which is the worst kind here.
    //
    //  A marker survives on a std::string now, so "has a requiredType" no
    //  longer means "is an entity reference". The generator went on choosing
    //  Ref, which takes an EntityRef - and the `if constexpr (requires ...)`
    //  guard around every generated call then SWALLOWED it. The build was
    //  clean, the Console said the variable was exposed, and the Inspector
    //  showed nothing at all.
    // ======================================================================
    const ScriptSourceInfo marked = ScanScriptSource(
        Wrap("    std::string walkAnimations[4];   // [animation] by direction"));
    const std::string unit = GenerateExposerUnit(marked, "scripts/Thing.cpp");

    CHECK(unit.find("f.Asset(") != std::string::npos);
    CHECK(unit.find("f.Ref(") == std::string::npos);
    CHECK(unit.find("\"animation\"") != std::string::npos);

    // An entity reference still gets Ref, which is the call that checks its
    // constraint against what is registered.
    const ScriptSourceInfo entity = ScanScriptSource(
        Wrap("    eng::EntityRef target;   // [Rigidbody2DComponent] what to push"));
    const std::string entityUnit = GenerateExposerUnit(entity, "scripts/Thing.cpp");

    CHECK(entityUnit.find("f.Ref(") != std::string::npos);
    CHECK(entityUnit.find("f.Asset(") == std::string::npos);

    // And an UNMARKED string is an ordinary variable with an ordinary box.
    const ScriptSourceInfo plain =
        ScanScriptSource(Wrap("    std::string label = \"hi\";   // what it says"));
    const std::string plainUnit = GenerateExposerUnit(plain, "scripts/Thing.cpp");

    CHECK(plainUnit.find("f.Var(\"label\"") != std::string::npos);
    CHECK(plainUnit.find("f.Asset(") == std::string::npos);
}

// ===========================================================================
//  A marker on the comment line DIRECTLY ABOVE the declaration
//
//  THE TRAILING COMMENT USED TO BE THE ONLY PLACE ONE FIT, and that is a
//  worse limitation than it sounds. The variables most in need of a marker
//  are the ones with the most explanation above them, so the marker ends up
//  at the far right of the declaration, past the tooltip, where a reader of
//  that explanation never looks.
//
//  How it was reported: a second array of animation names was added beside
//  one that had the marker, in the same shape, and came out as four plain
//  text boxes - the difference being a comment nobody had seen.
// ===========================================================================

TEST_CASE("a marker on the comment line directly above is read") {
    const ScriptSourceInfo info =
        ScanScriptSource(Wrap("    // [animation] one per direction\n"
                              "    std::string attackAnimations[4];"));

    const ScriptVarDecl* var = Var(info, "attackAnimations");
    REQUIRE(var != nullptr);
    CHECK(var->requiredType == "animation");
    CHECK(var->isArray);

    // With no comment of its own, what followed the marker above is the
    // tooltip - there is nowhere else for one to come from.
    CHECK(var->tooltip == "one per direction");
}

TEST_CASE("a range and a required type work from the line above as well") {
    // Nothing about the marker's MEANING changed, so every kind of marker
    // arrives by the new route too. Worth pinning: a change that only moved
    // where [animation] may be written would be a special case.
    const ScriptSourceInfo info =
        ScanScriptSource(Wrap("    // [0..10] how much to mix\n"
                              "    float blend = 0.5f;\n"
                              "    // [Rigidbody2DComponent] what to push\n"
                              "    eng::EntityRef target;"));

    const ScriptVarDecl* blend = Var(info, "blend");
    REQUIRE(blend != nullptr);
    CHECK(blend->ranged);
    CHECK(blend->min == doctest::Approx(0.0f));
    CHECK(blend->max == doctest::Approx(10.0f));
    CHECK(blend->tooltip == "how much to mix");

    const ScriptVarDecl* target = Var(info, "target");
    REQUIRE(target != nullptr);
    CHECK(target->requiredType == "Rigidbody2DComponent");
}

TEST_CASE("the declaration's own marker wins over the line above") {
    const ScriptSourceInfo info =
        ScanScriptSource(Wrap("    // [image] the one above\n"
                              "    std::string clip;   // [animation] the one beside"));

    const ScriptVarDecl* var = Var(info, "clip");
    REQUIRE(var != nullptr);
    CHECK(var->requiredType == "animation");
    CHECK(var->tooltip == "the one beside");
}

TEST_CASE("a trailing comment keeps the tooltip when the marker came from above") {
    const ScriptSourceInfo info =
        ScanScriptSource(Wrap("    // [animation] not this, the line below says it\n"
                              "    std::string clip;   // which clip to play"));

    const ScriptVarDecl* var = Var(info, "clip");
    REQUIRE(var != nullptr);
    CHECK(var->requiredType == "animation");
    CHECK(var->tooltip == "which clip to play");
}

TEST_CASE("a BLANK LINE between the comment and the declaration breaks it") {
    // "Immediately above" has to mean that, or a [TODO] eight lines up in a
    // comment block quietly becomes a required type.
    const ScriptSourceInfo info =
        ScanScriptSource(Wrap("    // [animation] not this one\n"
                              "\n"
                              "    std::string clip;"));

    const ScriptVarDecl* var = Var(info, "clip");
    REQUIRE(var != nullptr);
    CHECK(var->requiredType.empty());
}

TEST_CASE("a banner line between them breaks it too") {
    const ScriptSourceInfo info =
        ScanScriptSource(Wrap("    // [animation] not this one either\n"
                              "    // ---------------------------------\n"
                              "    std::string clip;"));

    const ScriptVarDecl* var = Var(info, "clip");
    REQUIRE(var != nullptr);
    CHECK(var->requiredType.empty());
}

TEST_CASE("prose on the line above is not read as a marker") {
    // The rule is the trailing comment's own: the bracket has to be at the
    // START. A comment block is documentation first.
    const ScriptSourceInfo info =
        ScanScriptSource(Wrap("    // see [1] for why this is 90 and not 100\n"
                              "    float radius = 90.0f;"));

    const ScriptVarDecl* var = Var(info, "radius");
    REQUIRE(var != nullptr);
    CHECK_FALSE(var->ranged);
    CHECK(var->requiredType.empty());
}

TEST_CASE("a marker from the line above reaches the generated Asset call") {
    // The parse is only half of it. This is the half the Inspector sees.
    const ScriptSourceInfo info =
        ScanScriptSource(Wrap("    // [animation] one per direction\n"
                              "    std::string attackAnimations[4];"));
    const std::string unit = GenerateExposerUnit(info, "scripts/Thing.cpp");

    CHECK(unit.find("f.Asset(") != std::string::npos);
    CHECK(unit.find("\"animation\"") != std::string::npos);
}

// ===========================================================================
//  A REFERENCE TO ANOTHER SCRIPT
// ===========================================================================

TEST_CASE("a script reference is a field, and takes its constraint from the type") {
    // The same bargain ComponentRef makes, for a class the GAME defines. The
    // difference is where the constraint comes from: a component carries
    // T::kTypeName, so that class reads it off the type; a script class has no
    // such thing, so the name is lifted out of the template argument HERE.
    //
    // It lands already matching, which is the whole reason one branch serves
    // both: `Bullet` is character-for-character what ENGINE_REGISTER_SCRIPT
    // stringizes, what the registry keys on, and what ScriptName() hands
    // EntityHasNamed.
    const ScriptSourceInfo info = ScanScriptSource(Wrap(R"CPP(
    ScriptRef<Bullet>      target;
    eng::ScriptRef<Bullet> spare;
    ScriptRef<Bullet>      squad[3];
    ScriptRef <Bullet>     spaced;
    ScriptRef<Bullet>      braced{};
)CPP"));

    for (const char* name : {"target", "spare", "spaced", "braced"}) {
        REQUIRE_MESSAGE(Var(info, name) != nullptr, name);
        CHECK(Var(info, name)->fieldType == ScriptFieldType::Entity);
        CHECK(Var(info, name)->requiredType == "Bullet");
    }

    // An array is one row per element, as it is for every other field type.
    REQUIRE(Var(info, "squad") != nullptr);
    CHECK(Var(info, "squad")->isArray);
    CHECK(Var(info, "squad")->requiredType == "Bullet");

    CHECK(info.skipped.empty());
}

TEST_CASE("a script reference and a component reference live side by side") {
    const ScriptSourceInfo info = ScanScriptSource(Wrap(R"CPP(
    ScriptRef<Bullet> target;
    SpriteRenderer    flash;
)CPP"));

    REQUIRE(Var(info, "target") != nullptr);
    REQUIRE(Var(info, "flash") != nullptr);
    CHECK(Var(info, "target")->requiredType == "Bullet");
    CHECK(Var(info, "flash")->requiredType == "SpriteComponent");
}

TEST_CASE("a marker still beats a script reference's own constraint") {
    // The type is the DEFAULT constraint, not an override of what was asked
    // for - the same rule the component case follows.
    const ScriptSourceInfo info =
        ScanScriptSource(Wrap("    ScriptRef<Bullet> target;   // [Rocket]"));

    REQUIRE(Var(info, "target") != nullptr);
    CHECK(Var(info, "target")->requiredType == "Rocket");
}

TEST_CASE("the closing angle bracket is counted, not guessed at") {
    // THE BUG THIS PINS DOWN was a silent one, which is the sort this file
    // exists to prevent: the closing '>' was found with rfind, so it was
    // whichever '>' came last ON THE LINE. With a '>' in the initialiser the
    // template argument came out as `Bullet> target = Pick<Bullet`, the
    // synthetic spelling swallowed the variable's own name, and the member was
    // neither exposed NOR listed as skipped - it simply ceased to exist.
    //
    // ComponentRef was wrong the same way, so both are checked here.
    const ScriptSourceInfo info = ScanScriptSource(Wrap(R"CPP(
    ScriptRef<Bullet> target = Pick<Bullet>();
    eng::ComponentRef<eng::SpriteComponent> flash = Pick<eng::SpriteComponent>();
)CPP"));

    REQUIRE_MESSAGE(Var(info, "target") != nullptr,
                    "the initialiser's '>' swallowed the variable name");
    CHECK(Var(info, "target")->requiredType == "Bullet");

    REQUIRE_MESSAGE(Var(info, "flash") != nullptr,
                    "the same bug, on the component spelling");
    CHECK(Var(info, "flash")->requiredType == "SpriteComponent");
}

TEST_CASE("a nested template argument keeps its whole spelling") {
    // Nothing registers a script under a nested name, so this never matches
    // anything at runtime and ScriptFields::Ref says so. What matters here is
    // that depth-counting does not stop at the inner '>' and mangle the name.
    const ScriptSourceInfo info =
        ScanScriptSource(Wrap("    ScriptRef<Outer<Inner>> odd;"));

    REQUIRE(Var(info, "odd") != nullptr);
    CHECK(Var(info, "odd")->requiredType == "Outer<Inner>");
}

TEST_CASE("an empty script reference argument is not a declaration") {
    // ScriptRef<T> static_asserts on T, but that is a COMPILER's job and this
    // parser never sees a type. The same answer the component case gives.
    const ScriptSourceInfo info = ScanScriptSource(Wrap("    ScriptRef<> nothing;"));
    CHECK(Var(info, "nothing") == nullptr);
}

// ===========================================================================
//  THE BARE SCRIPT NAME: `Door door;`
//
//  The spelling the reference feature exists for, and the one case this
//  parser cannot judge from the line alone. `Door door;` and
//  `Matrix4 basis;` are the same shape - two identifiers in a row - so the
//  only thing that tells them apart is whether some file in the project
//  registers a script called Door. That list is a PARAMETER, which is what
//  keeps the scan pure: no registry, no engine state, no file system.
// ===========================================================================

TEST_CASE("a bare script name is a reference to that script") {
    const std::vector<std::string> project{"Door", "Plate"};
    const ScriptSourceInfo info = ScanScriptSource(Wrap("    Door door;"), project);

    REQUIRE(Var(info, "door") != nullptr);
    CHECK(Var(info, "door")->fieldType == ScriptFieldType::Entity);
    CHECK(Var(info, "door")->requiredType == "Door");
}

TEST_CASE("a bare name that is not a script here is still not drawable") {
    // THE OTHER HALF OF THE SAME RULE. With nothing registered under that
    // name the member is a value of some type the Inspector cannot draw,
    // which is what it was before this feature and what it must stay - a
    // project with a Matrix4 must not find it turned into an entity slot.
    const std::vector<std::string> project{"Door"};
    const ScriptSourceInfo info = ScanScriptSource(Wrap("    Matrix4 basis;"), project);

    CHECK(Var(info, "basis") == nullptr);
    REQUIRE(Skipped(info, "basis") != nullptr);
    CHECK(Skipped(info, "basis")->reason.find("not one the Inspector can draw") !=
          std::string::npos);
}

TEST_CASE("the scan with no project list cannot see a bare script name") {
    // The one-argument overload, which every caller with no project behind it
    // uses - the Assets panel's "does this register anything", a lone file.
    // It has to behave exactly as it did before the feature existed.
    const ScriptSourceInfo info = ScanScriptSource(Wrap("    Door door;"));
    CHECK(Var(info, "door") == nullptr);
}

TEST_CASE("a bare script name with a star is still a pointer") {
    // `Door* door;` MUST NOT become a reference: the name is matched as a
    // whole word, so the star stops it and the member reaches the pointer
    // branch - whose message is the one that tells the author to drop it.
    const std::vector<std::string> project{"Door"};
    const ScriptSourceInfo info = ScanScriptSource(Wrap("    Door* door;"), project);

    CHECK(Var(info, "door") == nullptr);
    REQUIRE(Skipped(info, "door") != nullptr);
    CHECK(Skipped(info, "door")->reason.find("drop the star") != std::string::npos);
}

TEST_CASE("the explicit ScriptRef spelling still names the script") {
    // IT HAS TO KEEP WORKING, and this is the test that says so. A script's
    // bare name is now an alias for a reference to it, so `ScriptRef<Door>`
    // asks for a reference to a reference - which the partial specialization
    // in scene/ScriptRef.h collapses. What matters here is that the parser
    // still reports the constraint as the SCRIPT.
    const std::vector<std::string> project{"Door"};
    const ScriptSourceInfo         info =
        ScanScriptSource(Wrap("    ScriptRef<Door> door;"), project);

    REQUIRE(Var(info, "door") != nullptr);
    CHECK(Var(info, "door")->requiredType == "Door");
}

// ===========================================================================
//  THE GENERATED UNIT, which is where the bare name actually becomes one
// ===========================================================================

TEST_CASE("the generated unit wraps each script and aliases its bare name") {
    ScriptSourceInfo info;
    info.className = "Plate";

    const std::vector<ScriptUnitSource> sources{
        {"Door", "C:/p/Door.cpp", "#include <cmath>\nclass Door {};\n"},
        {"Plate", "C:/p/Plate.cpp", "class Plate { Door door; };\n"},
    };
    const std::string unit = GenerateScriptUnit(info, sources, "C:/gen/Plate__1.cpp");

    // Each script in a namespace of its own, each followed by the alias that
    // makes its bare name a reference everywhere else.
    CHECK(unit.find("namespace s_Door {") != std::string::npos);
    CHECK(unit.find("using Door = ::eng::ScriptRef<s_Door::Door>;") != std::string::npos);
    CHECK(unit.find("using Plate = ::eng::ScriptRef<s_Plate::Plate>;") !=
          std::string::npos);

    // THE ORDER IS THE ORDER GIVEN: Plate's body names Door, so Door has to
    // be complete first. Reversing this is the one thing that cannot compile.
    CHECK(unit.find("namespace s_Door") < unit.find("namespace s_Plate"));

    // AND THE INCLUDE CAME OUT. Left inside the namespace, <cmath> would have
    // put std::cos in s_Door - the failure that made hoisting necessary.
    REQUIRE(unit.find("#include <cmath>") != std::string::npos);
    CHECK(unit.find("#include <cmath>") < unit.find("namespace s_Door"));
}

TEST_CASE("hoisting an include leaves the line numbering alone") {
    // #line 1 claims the body starts at the author's line 1, so every line
    // after a hoisted include has to still be where it was. Without the blank
    // that stands in for it, every diagnostic below an include is reported
    // one line too high - per include, which is the sort of off-by-N that
    // makes a compiler error useless.
    ScriptSourceInfo info;
    info.className = "Door";

    const std::vector<ScriptUnitSource> sources{
        {"Door", "C:/p/Door.cpp",
         "#include <cmath>\n#include <string>\nclass Door {};\n"}};
    const std::string unit = GenerateScriptUnit(info, sources, "C:/gen/Door__1.cpp");

    const std::size_t marker = unit.find("#line 1 \"C:/p/Door.cpp\"");
    REQUIRE(marker != std::string::npos);
    const std::size_t bodyStart = unit.find('\n', marker) + 1;
    const std::size_t classAt   = unit.find("class Door {};", bodyStart);
    REQUIRE(classAt != std::string::npos);

    // Two blanks for the two hoisted includes, so `class` is still the third
    // line of the body exactly as it is the third line of the file.
    CHECK(unit.substr(bodyStart, classAt - bodyStart) == "\n\n");
}

TEST_CASE("a script with a hand-written OnExpose gets no generated exposer") {
    // THE HAND-WRITTEN LIST WINS, and two exposers for one script would mean
    // the generated one overwrote it. The wrapping still has to happen - the
    // class must be in its namespace in every unit or no library can cast to
    // it - so this is about the exposer alone.
    ScriptSourceInfo info;
    info.className   = "Door";
    info.hasOnExpose = true;

    const std::vector<ScriptUnitSource> sources{
        {"Door", "C:/p/Door.cpp", "class Door {};\n"}};
    const std::string unit = GenerateScriptUnit(info, sources, "C:/gen/Door__1.cpp");

    CHECK(unit.find("namespace s_Door {") != std::string::npos);
    CHECK(unit.find("SetExposer") == std::string::npos);
}

// ===========================================================================
//  A REFERENCE POINTING EITHER WAY, which is what makes this a feature rather
//  than a demonstration. The bug these cover: the alias used to be written
//  AFTER each namespace, so a script could only name the ones wrapped before
//  it - and no order puts both of a mutual pair first.
// ===========================================================================

TEST_CASE("every script is declared and aliased before any of them is defined") {
    ScriptSourceInfo info;
    info.className = "Door";

    // Door FIRST and naming DoorSwitch: the order that could not work while
    // the alias came after the namespace it belonged to.
    const std::vector<ScriptUnitSource> sources{
        {"Door", "C:/p/Door.cpp", "class Door { DoorSwitch plate; };\n"},
        {"DoorSwitch", "C:/p/DoorSwitch.cpp", "class DoorSwitch { Door door; };\n"},
    };
    const std::string unit = GenerateScriptUnit(info, sources, "C:/gen/Door__1.cpp");

    const std::size_t declared =
        unit.find("namespace s_DoorSwitch { class DoorSwitch; }");
    const std::size_t alias =
        unit.find("using DoorSwitch = ::eng::ScriptRef<s_DoorSwitch::DoorSwitch>;");
    const std::size_t firstBody = unit.find("namespace s_Door {\n");

    REQUIRE(declared != std::string::npos);
    REQUIRE(alias != std::string::npos);
    REQUIRE(firstBody != std::string::npos);

    // Both before the first script's text, which is the whole point: a
    // ScriptRef needs its T declared, never finished.
    CHECK(declared < firstBody);
    CHECK(alias < firstBody);
}

TEST_CASE("the alias is re-declared inside every other script's namespace") {
    // WHY IT HAS TO BE. Each namespace gets a using-directive for every other
    // one, so that one script can call another's file-scope helpers - and
    // that directive also drags in the other script's CLASS, which would be
    // ambiguous against the alias at global scope. A declaration in this
    // namespace beats anything a directive brought in, so the alias is
    // written again here. FORWARDS AS WELL AS BACK, or the pair above breaks
    // again for whichever one comes first.
    ScriptSourceInfo info;
    info.className = "Door";

    const std::vector<ScriptUnitSource> sources{
        {"Door", "C:/p/Door.cpp", "class Door { DoorSwitch plate; };\n"},
        {"DoorSwitch", "C:/p/DoorSwitch.cpp", "class DoorSwitch { Door door; };\n"},
    };
    const std::string unit = GenerateScriptUnit(info, sources, "C:/gen/Door__1.cpp");

    const std::size_t doorBody = unit.find("namespace s_Door {\n");
    REQUIRE(doorBody != std::string::npos);
    const std::size_t doorText = unit.find("#line 1 \"C:/p/Door.cpp\"");
    REQUIRE(doorText != std::string::npos);

    // Inside s_Door, before Door.cpp's own text: the directive, and the alias
    // that re-hides the class it brought in.
    const std::size_t directive = unit.find("using namespace s_DoorSwitch;", doorBody);
    const std::size_t hide =
        unit.find("using DoorSwitch = ::eng::ScriptRef<s_DoorSwitch::DoorSwitch>;",
                  doorBody);
    REQUIRE(directive != std::string::npos);
    REQUIRE(hide != std::string::npos);
    CHECK(directive < doorText);
    CHECK(hide < doorText);
}

TEST_CASE("a struct keeps its keyword in the declaration it gets") {
    // A `class Door;` declaration for a `struct Door` definition is legal and
    // earns warning C4099 for it - about a line in a generated file, naming a
    // difference the author cannot see from their own code.
    ScriptSourceInfo info;
    info.className = "Tally";

    std::vector<ScriptUnitSource> sources{
        {"Tally", "C:/p/Tally.cpp", "struct Tally {};\n", "struct"}};
    CHECK(GenerateScriptUnit(info, sources, "C:/gen/T__1.cpp")
              .find("namespace s_Tally { struct Tally; }") != std::string::npos);

    sources[0].classKey = "class";
    CHECK(GenerateScriptUnit(info, sources, "C:/gen/T__1.cpp")
              .find("namespace s_Tally { class Tally; }") != std::string::npos);
}

// ===========================================================================
//  THE FOUR TEXT QUESTIONS THE BUILD ASKS, which live beside the scan rather
//  than in the editor so that they can be asked without building a project.
// ===========================================================================

TEST_CASE("comments are blanked out, and nothing else is") {
    const std::string source =
        "int a;  // other->Method() is what a pair cannot do\n"
        "int b;  /* and\n"
        "          nor is this */\n"
        "const char* url = \"http://example.com/a//b\";\n";
    const std::string code = StripComments(source);

    // The code survives; the prose does not.
    CHECK(code.find("int a;") != std::string::npos);
    CHECK(code.find("int b;") != std::string::npos);
    CHECK(code.find("other->Method()") == std::string::npos);
    CHECK(code.find("nor is this") == std::string::npos);

    // A `//` INSIDE A STRING IS NOT A COMMENT, which is why literals are
    // tracked at all - a URL or a Windows path would otherwise take the rest
    // of its line with it.
    CHECK(code.find("\"http://example.com/a//b\"") != std::string::npos);

    // SAME LENGTH AND SAME LINES, so anything reported about one is true of
    // the other.
    REQUIRE(code.size() == source.size());
    CHECK(std::count(code.begin(), code.end(), '\n') ==
          std::count(source.begin(), source.end(), '\n'));
}

TEST_CASE("reaching through a reference is told from merely holding one") {
    // The distinction the wrapping order turns on. Only the arrow needs the
    // other script FINISHED; everything else goes through ScriptRef's own
    // members, which are compiled once the whole unit has been read.
    CHECK(ReachesThroughReference("if (plate) { plate->Open(); }", "plate"));
    CHECK(ReachesThroughReference("plate   ->Open();", "plate"));   // spaced

    CHECK_FALSE(ReachesThroughReference("DoorSwitch plate;", "plate"));
    CHECK_FALSE(ReachesThroughReference("if (plate) {}", "plate"));
    CHECK_FALSE(ReachesThroughReference("plate.IsSet();", "plate"));

    // A WHOLE WORD. `plateEdge->x` is a different member, and counting it
    // would invent an ordering edge - which in a mutual pair is the
    // difference between a build and a loop nobody wrote.
    CHECK_FALSE(ReachesThroughReference("plateEdge->Open();", "plate"));
    CHECK_FALSE(ReachesThroughReference("myplate->Open();", "plate"));
}

TEST_CASE("a base class is found, and the bare spelling told from the right one") {
    const std::vector<std::string> scripts{"Door", "DoorSwitch"};

    // `Door::ScriptType` is the spelling that works, because the bare name is
    // a reference and a base class has to be the class.
    const auto good = BaseScriptsOf("class Sliding final : public Door::ScriptType {};",
                                    "Sliding", scripts);
    REQUIRE(good.size() == 1);
    CHECK(good[0].name == "Door");
    CHECK(good[0].qualified);

    const auto bare =
        BaseScriptsOf("class Sliding final : public Door {};", "Sliding", scripts);
    REQUIRE(bare.size() == 1);
    CHECK(bare[0].name == "Door");
    CHECK_FALSE(bare[0].qualified);

    // ONLY THE BASE-CLAUSE. A script name in the BODY is a member's type, and
    // reading it as a base would order the build around a reference that does
    // not need it.
    CHECK(BaseScriptsOf("class Sliding final : public eng::ScriptBehaviour {\n"
                        "    Door door;\n};",
                        "Sliding", scripts)
              .empty());

    // And a script with no base-clause at all.
    CHECK(BaseScriptsOf("class Sliding {};", "Sliding", scripts).empty());
}

TEST_CASE("the class keyword is read off the definition, not guessed") {
    CHECK(ClassKeyOf("struct Tally final : public eng::ScriptBehaviour {};", "Tally") ==
          "struct");
    CHECK(ClassKeyOf("class Tally final : public eng::ScriptBehaviour {};", "Tally") ==
          "class");

    // A FORWARD DECLARATION IS NOT A DEFINITION, so it is not what the
    // keyword is taken from - and with nothing to read at all, `class` is the
    // answer, which is what every script written from the templates says.
    CHECK(ClassKeyOf("struct Tally;\nclass Tally {};", "Tally") == "class");
    CHECK(ClassKeyOf("nothing here at all", "Tally") == "class");
}

// ===========================================================================
//  THE HEADER AN IDE READS. What makes it different from a generated unit is
//  the one thing it has to get right: the alias for the script in the file
//  being edited cannot sit at global scope, because that file declares a
//  class of exactly that name there.
// ===========================================================================

TEST_CASE("the scope header puts the bare names in a namespace, not at global scope") {
    const std::vector<ScriptUnitSource> sources{
        {"Door", "C:/p/Door.cpp", "#include <cmath>\nclass Door { DoorSwitch p; };\n"},
        {"DoorSwitch", "C:/p/DoorSwitch.cpp", "class DoorSwitch { Door d; };\n"},
    };
    const std::string header = GenerateScriptScopeHeader(sources);

    // Every script declared, so the aliases can be formed.
    CHECK(header.find("namespace s_Door { class Door; }") != std::string::npos);
    CHECK(header.find("namespace s_DoorSwitch { class DoorSwitch; }") !=
          std::string::npos);

    // The aliases, inside the namespace that keeps them out of the way.
    const std::size_t block = header.find("namespace engine_script_names {");
    const std::size_t alias =
        header.find("using Door = ::eng::ScriptRef<s_Door::Door>;");
    const std::size_t directive = header.find("using namespace engine_script_names;");
    REQUIRE(block != std::string::npos);
    REQUIRE(alias != std::string::npos);
    REQUIRE(directive != std::string::npos);
    CHECK(block < alias);
    CHECK(alias < directive);

    // AND THE BODIES AFTER THE DIRECTIVE, so that a script's own text can see
    // the other scripts' names - the same order the generated unit uses.
    const std::size_t body = header.find("#line 1 \"C:/p/Door.cpp\"");
    REQUIRE(body != std::string::npos);
    CHECK(directive < body);

    // The include hoisted above all of it, for the reason a unit hoists it:
    // inside a namespace it would put std::cos in there too.
    const std::size_t hoisted = header.find("#include <cmath>");
    REQUIRE(hoisted != std::string::npos);
    CHECK(hoisted < block);

    // ENGINE'S OWN HEADER EXACTLY ONCE. #pragma once makes a repeat harmless,
    // and a generated file with a visibly duplicated line invites somebody to
    // wonder which copy is the mistake.
    std::size_t engineIncludes = 0;
    for (std::size_t at = header.find("#include <engine/Engine.h>");
         at != std::string::npos;
         at = header.find("#include <engine/Engine.h>", at + 1)) {
        ++engineIncludes;
    }
    CHECK(engineIncludes == 1);
}

TEST_CASE("the scope header requalifies the registrar, which is load-bearing") {
    // A script names itself unqualified exactly once, on its
    // ENGINE_REGISTER_SCRIPT line - and in this arrangement that name is both
    // the class the open file defines and a reference brought in by the
    // using-directive above. Measured without these two lines: `error C2872:
    // 'Gun': ambiguous symbol` on that line, in every script.
    const std::vector<ScriptUnitSource> sources{
        {"Door", "C:/p/Door.cpp", "class Door {};\n"}};
    const std::string header = GenerateScriptScopeHeader(sources);

    const std::size_t undef    = header.find("#undef ENGINE_REGISTER_SCRIPT");
    const std::size_t redefine = header.find(
        "#define ENGINE_REGISTER_SCRIPT(Type) ENGINE_SCRIPT_CHECKS(::Type)");
    REQUIRE(undef != std::string::npos);
    REQUIRE(redefine != std::string::npos);
    CHECK(undef < redefine);

    // LAST, so it applies to the file being edited and not to the copies
    // above it - those are inside their own namespaces, where the bare name
    // is the class and the ordinary macro is right.
    CHECK(redefine > header.find("#line 1 \"C:/p/Door.cpp\""));
}

TEST_CASE("the scope header declares a struct as a struct") {
    const std::vector<ScriptUnitSource> sources{
        {"Tally", "C:/p/Tally.cpp", "struct Tally {};\n", "struct"}};
    CHECK(GenerateScriptScopeHeader(sources).find(
              "namespace s_Tally { struct Tally; }") != std::string::npos);
}

TEST_CASE("a project with no scripts still gets a usable scope header") {
    // WRITTEN EVEN THEN, because the configuration names it by path and a
    // forcedInclude pointing at a file that does not exist is an error every
    // IDE reports against every file. A new project is this case.
    const std::string header = GenerateScriptScopeHeader({});

    CHECK(header.find("#pragma once") != std::string::npos);
    CHECK(header.find("#include <engine/Engine.h>") != std::string::npos);
    CHECK(header.find("namespace engine_script_names {") != std::string::npos);
    CHECK(header.find("#undef ENGINE_REGISTER_SCRIPT") != std::string::npos);
}
