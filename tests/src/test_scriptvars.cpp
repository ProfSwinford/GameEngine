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

TEST_CASE("an m_ prefix hides a variable, silently") {
    // ======================================================================
    //  This project names every internal member m_something, so the
    //  convention is already a statement of intent - it is this engine's
    //  [HideInInspector].
    //
    //  SILENTLY is the part that matters. TilemapWalker keeps a public
    //  `Ridigbody2D m_body`, which is a typedef for a POINTER - so the parser
    //  cannot see that it is one and would report the type instead, on every
    //  build, forever.
    // ======================================================================
    const ScriptSourceInfo info = ScanScriptSource(Wrap(R"CPP(
    float m_internal = 1.0f;
    Ridigbody2D m_body = nullptr;
    std::string m_name;
    float shown = 2.0f;
)CPP"));

    CHECK(Var(info, "m_internal") == nullptr);
    CHECK(Var(info, "m_body") == nullptr);
    CHECK(Var(info, "m_name") == nullptr);
    CHECK(Var(info, "shown") != nullptr);

    // Not reported either - the name said so.
    CHECK(info.skipped.empty());
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
)CPP"));

    REQUIRE(info.vars.size() == 11);
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
    // and the file's anonymous namespace intact, so a compiler error still
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
        if (eng::Transform2D* transform = Transform(); transform != nullptr) {
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
