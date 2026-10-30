// ============================================================================
//  test_scriptfields.cpp - the variables a script shows in the Inspector.
//
//  TWO LAYERS, AND THE SECOND ONE IS WHERE THE INTERESTING BUGS ARE.
//
//  The first is one value at a time: a field's type, the JSON it turns into,
//  and the JSON it will accept back. That part is arithmetic and is checked
//  exhaustively, including the shapes that must be REFUSED - a value the file
//  cannot hold is a value that silently reverts to a default, and the whole
//  point of this feature is that the number you see is the number that runs.
//
//  The second is the component, and it is about TIME. The defaults have to be
//  captured before the saved values are applied, the saved values have to
//  outlive the script object through a rebuild, and saving a scene has to
//  write what somebody set rather than what the game has since done to it.
//  Each of those is an ordering, and an ordering cannot be checked by looking
//  at the code - it has to be made to happen.
// ============================================================================

#include <doctest/doctest.h>

#include <engine/Engine.h>

#include <string>

using namespace eng;

namespace {

// ---------------------------------------------------------------------------
//  Script types to test against.
//
//  Registered for real, with ENGINE_REGISTER_SCRIPT, so these go through
//  exactly the path a script in assets/scripts does - including the hook
//  detection that has to notice OnExpose at all.
// ---------------------------------------------------------------------------

// One variable of every supported type, with defaults that are easy to
// recognise if something puts the wrong one back.
class TestAllTypes final : public ScriptBehaviour {
public:
    float       number  = 1.5f;
    int         count   = 7;
    bool        enabled = true;
    std::string label   = "hello";
    Vec2        offset  = Vec2{3.0f, -4.0f};
    Color       tint    = Color{10, 20, 30, 40};

    void OnExpose(ScriptFields& fields) {
        fields.Var("number", number, "a float with a note on it");
        fields.Var("count", count);
        fields.Var("enabled", enabled);
        fields.Var("label", label);
        fields.Var("offset", offset);
        fields.Var("tint", tint);
    }

    void OnFixedUpdate() { ++steps; }

    int steps = 0;
};

// References to other entities: one unconstrained, one that insists on a
// built-in component, one that insists on a script.
class TestRefs final : public ScriptBehaviour {
public:
    EntityRef anything;
    EntityRef needsSprite;
    EntityRef needsScript;

    // A DEFAULT THAT IS A NAME, written here in the code - which is how
    // GameRules.cpp points at its HUD labels out of the box. It behaves
    // differently from an empty one under a rename, and that difference is
    // where the bug was.
    EntityRef preset{"Preset"};

    void OnExpose(ScriptFields& fields) {
        fields.Var("anything", anything);
        fields.Ref("needsSprite", needsSprite, "SpriteComponent");
        fields.Ref("needsScript", needsScript, "TestPlain", "must carry a TestPlain");
        fields.Var("preset", preset);
    }

    void OnFixedUpdate() {}
};

// A slider, and the ends it was given.
class TestBounded final : public ScriptBehaviour {
public:
    float blend = 0.25f;
    int   lives = 3;

    void OnExpose(ScriptFields& fields) {
        fields.Range("blend", blend, 0.0f, 1.0f);
        fields.Range("lives", lives, 1, 9);
    }

    void OnFixedUpdate() {}
};

// No OnExpose at all, which is most scripts. Nothing about them should change.
class TestPlain final : public ScriptBehaviour {
public:
    void OnFixedUpdate() {}
};

// For the generated-exposer tests, which INSTALL one on it. Separate from
// TestPlain on purpose: ScriptRegistry is global and lives for the whole run,
// so a test that changes an entry must not change one another test relies on.
class TestAuto final : public ScriptBehaviour {
public:
    float speed = 7.5f;

    void OnFixedUpdate() {}
};

// And a second, because the "a null one is refused" case also installs an
// exposer - on a registry that is global and lives for the whole program. Two
// cases writing the same entry would pass or fail on the order doctest
// happened to pick, which is the worst kind of test.
class TestAutoAlt final : public ScriptBehaviour {
public:
    float speed = 1.0f;

    void OnFixedUpdate() {}
};

// A component bound to a script THE WAY A SCENE LOAD DOES IT: deserialized
// first, attached second. That order is not incidental - it is what puts the
// saved values in place before the script object exists, so they are applied
// during the bind rather than after OnStart has already read them.
struct Attached {
    ScriptComponent component;

    Attached(const char* scriptName, const Json& vars = Json::object()) {
        Json node;
        node["script"] = scriptName;
        if (!vars.empty()) {
            node["vars"] = vars;
        }
        std::string error;
        REQUIRE(component.Deserialize(node, error));
        component.OnAttach();
    }

    // Null when no variable has that name - which is also the answer for a
    // script that is not compiled in.
    const ScriptField* Field(const char* name) const {
        return component.Fields().Find(name);
    }
};

// A scene with entities in it, ACTIVE for as long as it is in scope.
//
// An entity reference resolves through Scene::Active() - see EntityRef.h for
// why that and not a remembered scene pointer - so a test about references
// needs a scene for them to resolve against. The previous active scene is put
// back afterwards, because the other test files in this binary load scenes of
// their own and one leaking out of here would be a failure somewhere else.
struct TestScene {
    Scene  scene;
    Scene* previous = nullptr;

    TestScene() {
        previous = Scene::Active();
        Scene::SetActive(&scene);
    }

    ~TestScene() {
        scene.Unload();
        Scene::SetActive(previous);
    }

    TestScene(const TestScene&)            = delete;
    TestScene& operator=(const TestScene&) = delete;

    Entity& Add(const char* name) {
        Entity* entity = scene.Get(scene.CreateEntity(name));
        REQUIRE(entity != nullptr);
        return *entity;
    }

    // An entity carrying a script, built the way a scene load does it.
    Entity& AddWithScript(const char* name, const char* scriptName) {
        Entity& entity = Add(name);
        auto    script = std::make_unique<ScriptComponent>();

        Json node;
        node["script"] = scriptName;
        std::string error;
        REQUIRE(script->Deserialize(node, error));
        entity.AddComponent(std::move(script));
        return entity;
    }
};

// A standalone field, for the value-level tests, with no script anywhere.
template <class T>
ScriptField Bind(T& value, const char* name = "v") {
    ScriptFields fields;
    fields.Var(name, value);
    REQUIRE(fields.Count() == 1);
    return fields.All()[0];
}

} // namespace

ENGINE_REGISTER_SCRIPT(TestAllTypes)
ENGINE_REGISTER_SCRIPT(TestBounded)
ENGINE_REGISTER_SCRIPT(TestPlain)
ENGINE_REGISTER_SCRIPT(TestRefs)
ENGINE_REGISTER_SCRIPT(TestAuto)
ENGINE_REGISTER_SCRIPT(TestAutoAlt)

// ===========================================================================
//  Collecting the list
// ===========================================================================

TEST_CASE("variables come back in the order they were declared") {
    float a = 0.0f, b = 0.0f, c = 0.0f;

    ScriptFields fields;
    fields.Var("first", a);
    fields.Var("second", b);
    fields.Var("third", c);

    REQUIRE(fields.Count() == 3);

    // Worth asserting rather than assuming. A script groups related settings
    // by listing them together, and a std::map keyed on the name - the
    // obvious implementation - would have put these in alphabetical order and
    // silently scrambled every script's layout.
    CHECK(fields.All()[0].name == "first");
    CHECK(fields.All()[1].name == "second");
    CHECK(fields.All()[2].name == "third");
}

TEST_CASE("a variable remembers its name, type and tooltip") {
    float speed = 0.0f;

    ScriptFields fields;
    fields.Var("speed", speed, "how fast");

    const ScriptField& field = fields.All()[0];
    CHECK(field.name == "speed");
    CHECK(field.tooltip == "how fast");
    CHECK(field.type == ScriptFieldType::Float);
    CHECK(field.value == &speed);
    CHECK_FALSE(field.ranged);
}

TEST_CASE("the name and tooltip are COPIED, not pointed at") {
    // The text handed to Var lives in the compiled script library, which is
    // unloaded and replaced whenever scripts are rebuilt. A view into it would
    // read correctly for a while and then point at unmapped memory, which is
    // the worst way for this to fail.
    float        value = 0.0f;
    ScriptFields fields;
    {
        const std::string name    = "temporary";
        const std::string tooltip = "also temporary";
        fields.Var(name, value, tooltip);
    }   // both strings are gone here

    CHECK(fields.All()[0].name == "temporary");
    CHECK(fields.All()[0].tooltip == "also temporary");
}

TEST_CASE("Find returns the variable with that name, or nothing") {
    float a = 0.0f, b = 0.0f;

    ScriptFields fields;
    fields.Var("alpha", a);
    fields.Var("beta", b);

    REQUIRE(fields.Find("beta") != nullptr);
    CHECK(fields.Find("beta")->value == &b);
    CHECK(fields.Find("gamma") == nullptr);
}

TEST_CASE("two variables cannot share a name - the first one wins") {
    // The name is the key in the scene file, so a duplicate would mean two
    // variables reading and writing one saved value. Keeping the first is the
    // only stable answer; the engine also reports it.
    float first = 1.0f, second = 2.0f;

    ScriptFields fields;
    fields.Var("speed", first);
    fields.Var("speed", second);

    CHECK(fields.Count() == 1);
    CHECK(fields.Find("speed")->value == &first);
}

TEST_CASE("a variable with no name is refused") {
    float value = 0.0f;

    ScriptFields fields;
    fields.Var("", value);

    CHECK(fields.Empty());
}

TEST_CASE("Clear empties the list") {
    float value = 0.0f;

    ScriptFields fields;
    fields.Var("v", value);
    REQUIRE(fields.Count() == 1);

    fields.Clear();
    CHECK(fields.Empty());
    CHECK(fields.Find("v") == nullptr);
}

// ===========================================================================
//  Ranges
// ===========================================================================

TEST_CASE("a Range keeps its ends") {
    float blend = 0.5f;

    ScriptFields fields;
    fields.Range("blend", blend, 0.0f, 1.0f);

    const ScriptField& field = fields.All()[0];
    CHECK(field.ranged);
    CHECK(field.min == doctest::Approx(0.0f));
    CHECK(field.max == doctest::Approx(1.0f));
}

TEST_CASE("an int Range keeps its ends as well") {
    int lives = 3;

    ScriptFields fields;
    fields.Range("lives", lives, 1, 9);

    CHECK(fields.All()[0].type == ScriptFieldType::Int);
    CHECK(fields.All()[0].ranged);
    CHECK(fields.All()[0].min == doctest::Approx(1.0f));
    CHECK(fields.All()[0].max == doctest::Approx(9.0f));
}

TEST_CASE("a backwards range is demoted to an ordinary number") {
    // A slider whose ends are the wrong way round cannot move. The variable
    // itself is fine, so it is still shown - as a drag box.
    float value = 5.0f;

    ScriptFields fields;
    fields.Range("value", value, 10.0f, 0.0f);

    REQUIRE(fields.Count() == 1);
    CHECK_FALSE(fields.All()[0].ranged);
}

TEST_CASE("a range with both ends the same is demoted too") {
    float value = 5.0f;

    ScriptFields fields;
    fields.Range("value", value, 1.0f, 1.0f);

    REQUIRE(fields.Count() == 1);
    CHECK_FALSE(fields.All()[0].ranged);
}

// ===========================================================================
//  Typed access
//
//  Every accessor checks the type first. Reading a std::string through
//  AsFloat() would not fail, it would reinterpret the string's internals as a
//  number - and writing through it would corrupt the string. A null is the only
//  acceptable answer to the wrong question.
// ===========================================================================

TEST_CASE("the right accessor returns the variable, every other one returns null") {
    float       number  = 1.0f;
    int         count   = 2;
    bool        enabled = false;
    std::string label   = "x";
    Vec2        offset  = Vec2{1.0f, 2.0f};
    Color       tint    = Color{1, 2, 3, 4};

    const ScriptField f = Bind(number);
    CHECK(f.AsFloat() == &number);
    CHECK(f.AsInt() == nullptr);
    CHECK(f.AsBool() == nullptr);
    CHECK(f.AsString() == nullptr);
    CHECK(f.AsVec2() == nullptr);
    CHECK(f.AsColor() == nullptr);

    CHECK(Bind(count).AsInt() == &count);
    CHECK(Bind(count).AsFloat() == nullptr);

    CHECK(Bind(enabled).AsBool() == &enabled);
    CHECK(Bind(enabled).AsInt() == nullptr);

    CHECK(Bind(label).AsString() == &label);
    CHECK(Bind(label).AsFloat() == nullptr);

    CHECK(Bind(offset).AsVec2() == &offset);
    CHECK(Bind(offset).AsColor() == nullptr);

    CHECK(Bind(tint).AsColor() == &tint);
    CHECK(Bind(tint).AsVec2() == nullptr);
}

TEST_CASE("each type reports its own name") {
    CHECK(std::string(ScriptFieldTypeName(ScriptFieldType::Float)) == "float");
    CHECK(std::string(ScriptFieldTypeName(ScriptFieldType::Int)) == "int");
    CHECK(std::string(ScriptFieldTypeName(ScriptFieldType::Bool)) == "bool");
    CHECK(std::string(ScriptFieldTypeName(ScriptFieldType::String)) == "text");
    CHECK(std::string(ScriptFieldTypeName(ScriptFieldType::Vec2)) == "position");
    CHECK(std::string(ScriptFieldTypeName(ScriptFieldType::Color)) == "colour");
}

// ===========================================================================
//  A value, written out
// ===========================================================================

TEST_CASE("a float is written as a plain number") {
    float value = 2.5f;
    CHECK(FieldToJson(Bind(value)).get<float>() == doctest::Approx(2.5f));
}

TEST_CASE("an int is written as an integer, not as 7.0") {
    int value = 7;

    const Json out = FieldToJson(Bind(value));
    CHECK(out.is_number_integer());
    CHECK(out.get<int>() == 7);
}

TEST_CASE("a bool is written as true or false") {
    bool value = true;

    const Json out = FieldToJson(Bind(value));
    CHECK(out.is_boolean());
    CHECK(out.get<bool>());
}

TEST_CASE("a Vec2 is written as two numbers, the shape ReadVec2 expects") {
    Vec2 value{12.5f, -4.0f};

    const Json out = FieldToJson(Bind(value));
    REQUIRE(out.is_array());
    REQUIRE(out.size() == 2);
    CHECK(out[0].get<float>() == doctest::Approx(12.5f));
    CHECK(out[1].get<float>() == doctest::Approx(-4.0f));
}

TEST_CASE("a Color is written as four whole numbers from 0 to 255") {
    Color value{255, 128, 0, 90};

    const Json out = FieldToJson(Bind(value));
    REQUIRE(out.is_array());
    REQUIRE(out.size() == 4);

    // INTEGERS, like SpriteComponent's tint. An unsigned char handed straight
    // to the JSON library is still a number, but a scene file reading
    // [255, 128, 0, 90] rather than a list of characters is the whole reason
    // the cast is written out.
    CHECK(out[0].is_number_integer());
    CHECK(out[0].get<int>() == 255);
    CHECK(out[1].get<int>() == 128);
    CHECK(out[2].get<int>() == 0);
    CHECK(out[3].get<int>() == 90);
}

// ===========================================================================
//  A value, read back
// ===========================================================================

TEST_CASE("every type survives a round trip through JSON") {
    float       number  = 1.5f;
    int         count   = 7;
    bool        enabled = true;
    std::string label   = "hello";
    Vec2        offset  = Vec2{3.0f, -4.0f};
    Color       tint    = Color{10, 20, 30, 40};

    // Written out, the variable scribbled on, then read back - so a round trip
    // that merely left everything alone could not pass.
    const ScriptField numberField = Bind(number);
    const Json        numberJson  = FieldToJson(numberField);
    number                        = -99.0f;

    std::string error;
    REQUIRE(ReadFieldFromJson(numberField, numberJson, error));
    CHECK(number == doctest::Approx(1.5f));

    const ScriptField countField = Bind(count);
    const Json        countJson  = FieldToJson(countField);
    count                        = -99;
    REQUIRE(ReadFieldFromJson(countField, countJson, error));
    CHECK(count == 7);

    const ScriptField boolField = Bind(enabled);
    const Json        boolJson  = FieldToJson(boolField);
    enabled                     = false;
    REQUIRE(ReadFieldFromJson(boolField, boolJson, error));
    CHECK(enabled);

    const ScriptField labelField = Bind(label);
    const Json        labelJson  = FieldToJson(labelField);
    label                        = "scribbled";
    REQUIRE(ReadFieldFromJson(labelField, labelJson, error));
    CHECK(label == "hello");

    const ScriptField offsetField = Bind(offset);
    const Json        offsetJson  = FieldToJson(offsetField);
    offset                        = Vec2{0.0f, 0.0f};
    REQUIRE(ReadFieldFromJson(offsetField, offsetJson, error));
    CHECK(offset.x == doctest::Approx(3.0f));
    CHECK(offset.y == doctest::Approx(-4.0f));

    const ScriptField tintField = Bind(tint);
    const Json        tintJson  = FieldToJson(tintField);
    tint                        = Color{0, 0, 0, 0};
    REQUIRE(ReadFieldFromJson(tintField, tintJson, error));
    CHECK(tint.r == 10);
    CHECK(tint.g == 20);
    CHECK(tint.b == 30);
    CHECK(tint.a == 40);
}

TEST_CASE("a float accepts a whole number from a hand-written file") {
    // `"radius": 120` is what somebody types, and refusing it would be
    // pedantry rather than safety.
    float       value = 0.0f;
    std::string error;

    REQUIRE(ReadFieldFromJson(Bind(value), Json(120), error));
    CHECK(value == doctest::Approx(120.0f));
}

TEST_CASE("an int accepts a number with a decimal point, and rounds it") {
    int         value = 0;
    std::string error;

    REQUIRE(ReadFieldFromJson(Bind(value), Json(3.0), error));
    CHECK(value == 3);

    REQUIRE(ReadFieldFromJson(Bind(value), Json(3.7), error));
    CHECK(value == 4);

    REQUIRE(ReadFieldFromJson(Bind(value), Json(-3.7), error));
    CHECK(value == -4);
}

TEST_CASE("an int is clamped rather than cast from an impossible number") {
    // 1e30 is a perfectly well-formed thing to find in a text file, and
    // casting it straight to int is undefined behaviour - which on this
    // compiler quietly produces a large negative number.
    int         value = 0;
    std::string error;

    REQUIRE(ReadFieldFromJson(Bind(value), Json(1e30), error));
    CHECK(value == std::numeric_limits<int>::max());

    REQUIRE(ReadFieldFromJson(Bind(value), Json(-1e30), error));
    CHECK(value == std::numeric_limits<int>::min());
}

TEST_CASE("colour channels outside 0-255 are pulled in") {
    Color       value{0, 0, 0, 0};
    std::string error;

    REQUIRE(ReadFieldFromJson(Bind(value), Json::array({300, -20, 128, 255}), error));
    CHECK(value.r == 255);
    CHECK(value.g == 0);
    CHECK(value.b == 128);
    CHECK(value.a == 255);
}

TEST_CASE("a bool refuses 1 and 0") {
    // Unlike the numeric types, this one is strict. `"jumping": 1` is as
    // likely to be a leftover from a count as an intended true, and guessing
    // would hide the mistake instead of reporting it.
    bool        value = false;
    std::string error;

    CHECK_FALSE(ReadFieldFromJson(Bind(value), Json(1), error));
    CHECK_FALSE(value);
    CHECK_FALSE(error.empty());

    CHECK_FALSE(ReadFieldFromJson(Bind(value), Json("true"), error));
    CHECK_FALSE(value);
}

TEST_CASE("text refuses a number") {
    std::string value = "kept";
    std::string error;

    CHECK_FALSE(ReadFieldFromJson(Bind(value), Json(12), error));
    CHECK(value == "kept");
}

TEST_CASE("a number refuses text") {
    float       value = 1.0f;
    std::string error;

    CHECK_FALSE(ReadFieldFromJson(Bind(value), Json("fast"), error));
    CHECK(value == doctest::Approx(1.0f));

    // The message says what was expected AND what was found, because a scene
    // file is hand-edited and "wrong type" on its own sends you looking.
    CHECK(error.find("float") != std::string::npos);
    CHECK(error.find("string") != std::string::npos);
}

TEST_CASE("a Vec2 refuses a list of the wrong length") {
    Vec2        value{1.0f, 2.0f};
    std::string error;

    CHECK_FALSE(ReadFieldFromJson(Bind(value), Json::array({1.0}), error));
    CHECK_FALSE(ReadFieldFromJson(Bind(value), Json::array({1.0, 2.0, 3.0}), error));
    CHECK_FALSE(ReadFieldFromJson(Bind(value), Json(5.0), error));

    CHECK(value.x == doctest::Approx(1.0f));
    CHECK(value.y == doctest::Approx(2.0f));
}

TEST_CASE("a half-valid Vec2 leaves BOTH halves alone") {
    // The shape is checked before a single number is written. Without that,
    // the x would already have been changed by the time the y was found to be
    // a string - so a bad line in a scene file would leave the variable
    // holding a value that appears nowhere, in the code or in the file.
    Vec2        value{7.0f, 8.0f};
    std::string error;

    CHECK_FALSE(ReadFieldFromJson(Bind(value), Json::array({99.0, "oops"}), error));
    CHECK(value.x == doctest::Approx(7.0f));
    CHECK(value.y == doctest::Approx(8.0f));
}

TEST_CASE("a half-valid Color leaves every channel alone") {
    Color       value{1, 2, 3, 4};
    std::string error;

    CHECK_FALSE(
        ReadFieldFromJson(Bind(value), Json::array({255, 255, 255, "oops"}), error));
    CHECK(value.r == 1);
    CHECK(value.g == 2);
    CHECK(value.b == 3);
    CHECK(value.a == 4);
}

TEST_CASE("no value a scene file could hold makes reading throw") {
    // ======================================================================
    //  A SCENE FILE IS TYPED BY HAND, so an exception out of the middle of
    //  loading one is a level that refuses to open over a typo. Json.h makes
    //  the same promise about its own readers and for the same reason.
    //
    //  Every type is tried against every hostile shape - 42 combinations -
    //  rather than the one or two that look dangerous. The library throws
    //  type_error on a mismatched get<>, and which pairs reach a get<> at all
    //  is exactly the thing a change here could get wrong.
    // ======================================================================
    float       number  = 1.0f;
    int         count   = 1;
    bool        enabled = false;
    std::string label   = "kept";
    Vec2        offset  = Vec2{1.0f, 2.0f};
    Color       tint    = Color{1, 2, 3, 4};

    const ScriptField everyField[] = {Bind(number), Bind(count),  Bind(enabled),
                                      Bind(label),  Bind(offset), Bind(tint)};

    const Json hostile[] = {
        Json(),                                   // null
        Json::object(),                           // a set of named values
        Json::array(),                            // an empty list
        Json::array({1}),                         // too short for anything
        Json::array({1, 2, 3, 4, 5}),             // too long for anything
        Json::array({"a", "b"}),                  // the right length, wrong insides

        // HOSTILE TO FIVE OF THE SIX AND PERFECTLY VALID FOR THE OTHER. It
        // stays in the list because the text field accepting it is what
        // proves this sweep is doing something: a matrix where every cell
        // refuses everything would pass just as well if the function did
        // nothing at all.
        Json("text"),
    };

    for (const ScriptField& field : everyField) {
        for (const Json& value : hostile) {
            std::string error;
            CHECK_NOTHROW(ReadFieldFromJson(field, value, error));
        }
    }

    // Nothing was written by any of the REFUSED ones, which is the other half
    // of the promise: a value the file cannot hold costs you that one
    // override and nothing else.
    CHECK(number == doctest::Approx(1.0f));
    CHECK(count == 1);
    CHECK_FALSE(enabled);
    CHECK(offset.x == doctest::Approx(1.0f));
    CHECK(offset.y == doctest::Approx(2.0f));
    CHECK(tint.a == 4);

    // ...and the one pair in the matrix that was NOT refused did go through.
    CHECK(label == "text");
}

TEST_CASE("JsonFitsFieldType agrees with what ReadFieldFromJson will take") {
    CHECK(JsonFitsFieldType(ScriptFieldType::Float, Json(1)));
    CHECK(JsonFitsFieldType(ScriptFieldType::Float, Json(1.5)));
    CHECK_FALSE(JsonFitsFieldType(ScriptFieldType::Float, Json("1.5")));

    CHECK(JsonFitsFieldType(ScriptFieldType::Int, Json(1.5)));
    CHECK_FALSE(JsonFitsFieldType(ScriptFieldType::Int, Json(true)));

    CHECK(JsonFitsFieldType(ScriptFieldType::Bool, Json(false)));
    CHECK_FALSE(JsonFitsFieldType(ScriptFieldType::Bool, Json(0)));

    CHECK(JsonFitsFieldType(ScriptFieldType::String, Json("")));
    CHECK_FALSE(JsonFitsFieldType(ScriptFieldType::String, Json(0)));

    CHECK(JsonFitsFieldType(ScriptFieldType::Vec2, Json::array({1, 2})));
    CHECK_FALSE(JsonFitsFieldType(ScriptFieldType::Vec2, Json::array({1, 2, 3})));

    CHECK(JsonFitsFieldType(ScriptFieldType::Color, Json::array({1, 2, 3, 4})));
    CHECK_FALSE(JsonFitsFieldType(ScriptFieldType::Color, Json::array({1, 2, 3})));
}

// ===========================================================================
//  The component: which hooks were found
// ===========================================================================

TEST_CASE("OnExpose is noticed by the compiler, like every other hook") {
    const ScriptRegistry::Entry* entry = ScriptRegistry::Find("TestAllTypes");
    REQUIRE(entry != nullptr);
    CHECK(entry->hooks.expose != nullptr);

    // And absent from a script that did not write one, which is what makes an
    // unexposed script cost nothing.
    const ScriptRegistry::Entry* plain = ScriptRegistry::Find("TestPlain");
    REQUIRE(plain != nullptr);
    CHECK(plain->hooks.expose == nullptr);
}

// ===========================================================================
//  An exposer installed from outside the class
//
//  This is how a plain `float speed = 7.5f;` reaches the Inspector with
//  nothing written down: the editor reads the source, generates a translation
//  unit that calls SetExposer, and compiles that instead of the original. See
//  engine/tools/ScriptVars.h.
// ===========================================================================

TEST_CASE("an installed exposer gives a script variables it never declared") {
    REQUIRE(ScriptRegistry::Find("TestAuto") != nullptr);

    // Exactly the shape the generator emits, including the generic lambda
    // that makes a wrong guess compile away rather than fail.
    //
    // Nothing else installs one on TestAuto - the case below uses its own
    // script for exactly that reason - so this is the first and it takes.
    const bool installed = ScriptRegistry::SetExposer(
        "TestAuto", [](ScriptBehaviour* behaviour, ScriptFields& fields) {
            auto* self = static_cast<TestAuto*>(behaviour);
            []<class S>(S* s, ScriptFields& f) {
                if constexpr (requires { f.Var("speed", s->speed); }) {
                    f.Var("speed", s->speed, "from the source");
                }
            }(self, fields);
        });
    CHECK(installed);

    const Attached attached("TestAuto");
    REQUIRE(attached.component.Fields().Count() == 1);
    CHECK(attached.Field("speed") != nullptr);
    CHECK(*attached.Field("speed")->AsFloat() == doctest::Approx(7.5f));
    CHECK(attached.Field("speed")->tooltip == "from the source");
}

TEST_CASE("a HAND-WRITTEN OnExpose is never overwritten") {
    // ======================================================================
    //  Somebody who wrote that function stated exactly what they wanted, and
    //  a list read out of the source has no business arguing with it. Checked
    //  in the registry rather than in the generator, so it holds however the
    //  call arrives.
    // ======================================================================
    REQUIRE(ScriptRegistry::Find("TestAllTypes") != nullptr);
    REQUIRE(ScriptRegistry::Find("TestAllTypes")->hooks.expose != nullptr);

    const bool installed = ScriptRegistry::SetExposer(
        "TestAllTypes",
        [](ScriptBehaviour*, ScriptFields& fields) { (void)fields; });
    CHECK_FALSE(installed);

    // And the real one is still in place: six variables, not none.
    const Attached attached("TestAllTypes");
    CHECK(attached.component.Fields().Count() == 6);
}

TEST_CASE("an exposer for a script that does not exist is refused") {
    CHECK_FALSE(ScriptRegistry::SetExposer(
        "NoSuchScriptIsCompiledIn",
        [](ScriptBehaviour*, ScriptFields& fields) { (void)fields; }));
}

TEST_CASE("a second exposer, and a null one, leave the first in place") {
    // SELF-CONTAINED rather than relying on the test above having run.
    // ScriptRegistry is global and lives for the whole program, so a test
    // that reads registry state another test wrote is a test that passes or
    // fails depending on the order doctest happens to pick.
    ScriptRegistry::SetExposer(
        "TestAutoAlt", [](ScriptBehaviour*, ScriptFields& fields) { (void)fields; });
    REQUIRE(ScriptRegistry::Find("TestAutoAlt")->hooks.expose != nullptr);

    CHECK_FALSE(ScriptRegistry::SetExposer("TestAutoAlt", nullptr));
    CHECK(ScriptRegistry::Find("TestAutoAlt")->hooks.expose != nullptr);
}

TEST_CASE("DescribeHooks names OnExpose") {
    const ScriptRegistry::Entry* entry = ScriptRegistry::Find("TestAllTypes");
    REQUIRE(entry != nullptr);
    CHECK(DescribeHooks(entry->hooks).find("OnExpose") != std::string::npos);
}

TEST_CASE("a script with no OnExpose has no variables and nothing saved") {
    const Attached attached("TestPlain");

    REQUIRE(attached.component.IsResolved());
    CHECK(attached.component.Fields().Empty());
    CHECK(attached.component.SavedValues().empty());
    CHECK(attached.component.UnusedSavedValues().empty());
}

TEST_CASE("binding a script collects its variables") {
    const Attached attached("TestAllTypes");

    REQUIRE(attached.component.IsResolved());
    REQUIRE(attached.component.Fields().Count() == 6);

    REQUIRE(attached.Field("number") != nullptr);
    CHECK(attached.Field("number")->type == ScriptFieldType::Float);
    CHECK(attached.Field("number")->tooltip == "a float with a note on it");

    CHECK(attached.Field("count")->type == ScriptFieldType::Int);
    CHECK(attached.Field("enabled")->type == ScriptFieldType::Bool);
    CHECK(attached.Field("label")->type == ScriptFieldType::String);
    CHECK(attached.Field("offset")->type == ScriptFieldType::Vec2);
    CHECK(attached.Field("tint")->type == ScriptFieldType::Color);
}

TEST_CASE("a bound variable points at the live script object") {
    const Attached attached("TestAllTypes");

    // The default from the code, read through the field rather than from the
    // class - which is the only way the editor ever sees it.
    REQUIRE(attached.Field("number")->AsFloat() != nullptr);
    CHECK(*attached.Field("number")->AsFloat() == doctest::Approx(1.5f));
    CHECK(*attached.Field("count")->AsInt() == 7);
    CHECK(*attached.Field("enabled")->AsBool());
    CHECK(*attached.Field("label")->AsString() == "hello");
    CHECK(attached.Field("offset")->AsVec2()->x == doctest::Approx(3.0f));
    CHECK(attached.Field("tint")->AsColor()->g == 20);
}

TEST_CASE("a range survives the trip through the hook table") {
    const Attached attached("TestBounded");

    REQUIRE(attached.Field("blend") != nullptr);
    CHECK(attached.Field("blend")->ranged);
    CHECK(attached.Field("blend")->max == doctest::Approx(1.0f));
    CHECK(attached.Field("lives")->ranged);
    CHECK(attached.Field("lives")->max == doctest::Approx(9.0f));
}

// ===========================================================================
//  Loading values out of a scene
// ===========================================================================

TEST_CASE("a saved value reaches the script before it runs") {
    Json vars;
    vars["number"] = 42.0;

    const Attached attached("TestAllTypes", vars);

    CHECK(*attached.Field("number")->AsFloat() == doctest::Approx(42.0f));
    CHECK(attached.component.IsFieldOverridden("number"));
}

TEST_CASE("a variable with nothing saved keeps the value from the code") {
    Json vars;
    vars["number"] = 42.0;

    const Attached attached("TestAllTypes", vars);

    // Five of the six were not mentioned in the file, so they are the code's.
    CHECK(*attached.Field("count")->AsInt() == 7);
    CHECK(*attached.Field("label")->AsString() == "hello");
    CHECK_FALSE(attached.component.IsFieldOverridden("count"));
    CHECK_FALSE(attached.component.IsFieldOverridden("label"));
}

TEST_CASE("every type can be loaded from a scene file") {
    Json vars;
    vars["number"]  = 9.25;
    vars["count"]   = 11;
    vars["enabled"] = false;
    vars["label"]   = "loaded";
    vars["offset"]  = Json::array({100.0, 200.0});
    vars["tint"]    = Json::array({1, 2, 3, 4});

    const Attached attached("TestAllTypes", vars);

    CHECK(*attached.Field("number")->AsFloat() == doctest::Approx(9.25f));
    CHECK(*attached.Field("count")->AsInt() == 11);
    CHECK_FALSE(*attached.Field("enabled")->AsBool());
    CHECK(*attached.Field("label")->AsString() == "loaded");
    CHECK(attached.Field("offset")->AsVec2()->y == doctest::Approx(200.0f));
    CHECK(attached.Field("tint")->AsColor()->a == 4);
}

TEST_CASE("a saved value of the wrong type leaves the code's value in place") {
    Json vars;
    vars["number"] = "fast";     // not a number
    vars["count"]  = 11;         // and this one is fine

    const Attached attached("TestAllTypes", vars);

    CHECK(*attached.Field("number")->AsFloat() == doctest::Approx(1.5f));

    // THE REST OF THE BLOCK STILL APPLIES. One bad line costs that one value,
    // not every setting on the component.
    CHECK(*attached.Field("count")->AsInt() == 11);

    // And the bad value is KEPT rather than dropped, so saving the scene does
    // not quietly rewrite the file somebody has to go and fix.
    CHECK(attached.component.SavedValues().contains("number"));
}

TEST_CASE("a \"vars\" that is not a set of named values is ignored, not fatal") {
    Json node;
    node["script"] = "TestAllTypes";
    node["vars"]   = Json::array({1, 2, 3});

    ScriptComponent component;
    std::string     error;

    // The SCRIPT is still fine, and refusing the whole component would cost
    // the entity its behaviour as well as its values.
    CHECK(component.Deserialize(node, error));
    component.OnAttach();

    CHECK(component.IsResolved());
    CHECK(component.SavedValues().empty());
    CHECK(*component.Fields().Find("number")->AsFloat() == doctest::Approx(1.5f));
}

TEST_CASE("a component with no \"vars\" at all is the ordinary case") {
    const Attached attached("TestAllTypes");

    CHECK(attached.component.SavedValues().empty());
    CHECK(*attached.Field("number")->AsFloat() == doctest::Approx(1.5f));
}

// ===========================================================================
//  Saving values back out
// ===========================================================================

TEST_CASE("a scene with nothing overridden has no \"vars\" key") {
    const Attached attached("TestAllTypes");

    Json out;
    REQUIRE(attached.component.Serialize(out));

    CHECK(out["script"] == "TestAllTypes");

    // ONLY WHAT SOMEBODY CHANGED. Writing all six defaults into every scene
    // would mean that changing one in the code reached nothing - every entity
    // would be carrying a copy of the old value.
    CHECK_FALSE(out.contains("vars"));
}

TEST_CASE("an edited variable is recorded and written out") {
    Attached attached("TestAllTypes");

    // What the Inspector does: write through the field's own pointer, then ask
    // the component to record it.
    *attached.Field("number")->AsFloat() = 12.0f;
    REQUIRE(attached.component.SaveFieldValue("number"));

    CHECK(attached.component.IsFieldOverridden("number"));

    Json out;
    REQUIRE(attached.component.Serialize(out));
    REQUIRE(out.contains("vars"));
    CHECK(out["vars"]["number"].get<float>() == doctest::Approx(12.0f));

    // And still only the one.
    CHECK(out["vars"].size() == 1);
}

TEST_CASE("SaveFieldValue refuses a name no variable has") {
    Attached attached("TestAllTypes");

    CHECK_FALSE(attached.component.SaveFieldValue("nonsense"));
    CHECK(attached.component.SavedValues().empty());
}

TEST_CASE("WHAT THE GAME DID to a variable is not saved") {
    // ======================================================================
    //  THE ASYMMETRY THAT MATTERS MOST IN THIS FILE.
    //
    //  The obvious Serialize asks the running object for its variables, and
    //  it is wrong: a scene saved after a minute of play would come back with
    //  the player on three health and a timer part-way through. What is
    //  written is what somebody set in the Inspector - the only thing they
    //  asked to be saved.
    // ======================================================================
    Attached attached("TestAllTypes");

    // The script changes its own variable, as a script is entitled to do.
    *attached.Field("number")->AsFloat() = -777.0f;

    Json out;
    REQUIRE(attached.component.Serialize(out));
    CHECK_FALSE(out.contains("vars"));
}

TEST_CASE("an overridden variable saves what was set, not what the game did") {
    Attached attached("TestAllTypes");

    *attached.Field("number")->AsFloat() = 12.0f;
    REQUIRE(attached.component.SaveFieldValue("number"));

    // ...and then the game moves it.
    *attached.Field("number")->AsFloat() = -777.0f;

    Json out;
    REQUIRE(attached.component.Serialize(out));
    CHECK(out["vars"]["number"].get<float>() == doctest::Approx(12.0f));
}

// ===========================================================================
//  Reverting
// ===========================================================================

TEST_CASE("revert puts back the value from the code, not the one from the file") {
    // ======================================================================
    //  THIS IS WHY THE DEFAULTS ARE SNAPSHOT BEFORE THE SAVED VALUES ARE
    //  APPLIED, and the ordering cannot be checked any other way.
    //
    //  Capture them a moment later - after the override has landed - and this
    //  test gets 42 back and calls it the default. The number would then be
    //  impossible to get rid of: every revert would restore the thing being
    //  reverted.
    // ======================================================================
    Json vars;
    vars["number"] = 42.0;

    Attached attached("TestAllTypes", vars);
    REQUIRE(*attached.Field("number")->AsFloat() == doctest::Approx(42.0f));

    REQUIRE(attached.component.RevertFieldValue("number"));

    CHECK(*attached.Field("number")->AsFloat() == doctest::Approx(1.5f));
    CHECK_FALSE(attached.component.IsFieldOverridden("number"));
}

TEST_CASE("revert forgets the saved value so the scene stops carrying it") {
    Json vars;
    vars["number"] = 42.0;
    vars["count"]  = 11;

    Attached attached("TestAllTypes", vars);
    REQUIRE(attached.component.RevertFieldValue("number"));

    Json out;
    REQUIRE(attached.component.Serialize(out));
    REQUIRE(out.contains("vars"));
    CHECK_FALSE(out["vars"].contains("number"));
    CHECK(out["vars"].contains("count"));
}

TEST_CASE("reverting the last override drops \"vars\" from the file entirely") {
    Json vars;
    vars["number"] = 42.0;

    Attached attached("TestAllTypes", vars);
    REQUIRE(attached.component.RevertFieldValue("number"));

    Json out;
    REQUIRE(attached.component.Serialize(out));
    CHECK_FALSE(out.contains("vars"));
}

TEST_CASE("every type can be reverted") {
    Json vars;
    vars["number"]  = 9.25;
    vars["count"]   = 11;
    vars["enabled"] = false;
    vars["label"]   = "loaded";
    vars["offset"]  = Json::array({100.0, 200.0});
    vars["tint"]    = Json::array({1, 2, 3, 4});

    Attached attached("TestAllTypes", vars);

    for (const char* name : {"number", "count", "enabled", "label", "offset", "tint"}) {
        CHECK(attached.component.RevertFieldValue(name));
    }

    CHECK(*attached.Field("number")->AsFloat() == doctest::Approx(1.5f));
    CHECK(*attached.Field("count")->AsInt() == 7);
    CHECK(*attached.Field("enabled")->AsBool());
    CHECK(*attached.Field("label")->AsString() == "hello");
    CHECK(attached.Field("offset")->AsVec2()->x == doctest::Approx(3.0f));
    CHECK(attached.Field("offset")->AsVec2()->y == doctest::Approx(-4.0f));
    CHECK(attached.Field("tint")->AsColor()->r == 10);
    CHECK(attached.Field("tint")->AsColor()->a == 40);
    CHECK(attached.component.SavedValues().empty());
}

TEST_CASE("revert refuses a name no variable has") {
    Attached attached("TestAllTypes");
    CHECK_FALSE(attached.component.RevertFieldValue("nonsense"));
}

TEST_CASE("reverting a variable that was never overridden is harmless") {
    Attached attached("TestAllTypes");

    CHECK(attached.component.RevertFieldValue("number"));
    CHECK(*attached.Field("number")->AsFloat() == doctest::Approx(1.5f));
    CHECK(attached.component.SavedValues().empty());
}

// ===========================================================================
//  A value outliving the script object
// ===========================================================================

TEST_CASE("saved values survive a script reload") {
    // What the editor does when scripts are rebuilt: every behaviour object is
    // destroyed along with the library that defined it, and then recreated.
    // The values are held by the COMPONENT, so they have to come back.
    Json vars;
    vars["number"] = 42.0;

    Attached attached("TestAllTypes", vars);

    attached.component.UnbindForReload();

    // Nothing is bound, so there are no variables to point at - but the data
    // is still here.
    CHECK_FALSE(attached.component.IsResolved());
    CHECK(attached.component.Fields().Empty());
    CHECK(attached.component.SavedValues().contains("number"));

    attached.component.RebindAfterReload();

    REQUIRE(attached.component.IsResolved());
    REQUIRE(attached.Field("number") != nullptr);
    CHECK(*attached.Field("number")->AsFloat() == doctest::Approx(42.0f));
}

TEST_CASE("the defaults come back after a reload too, so revert still works") {
    Json vars;
    vars["number"] = 42.0;

    Attached attached("TestAllTypes", vars);
    attached.component.UnbindForReload();
    attached.component.RebindAfterReload();

    REQUIRE(attached.component.RevertFieldValue("number"));
    CHECK(*attached.Field("number")->AsFloat() == doctest::Approx(1.5f));
}

TEST_CASE("a script that is not compiled in keeps its values untouched") {
    // ======================================================================
    //  THE CASE THIS IS ALL STORED AS JSON FOR.
    //
    //  With no class to ask, there is nothing to say that "speed" is a float -
    //  so the text is kept exactly as it was read and written back out
    //  unchanged. Saving a scene in a build that is missing a script must not
    //  quietly empty that script's settings.
    // ======================================================================
    Json vars;
    vars["speed"]   = 320.0;
    vars["nickname"] = "Scout";
    vars["colours"]  = Json::array({1, 2, 3, 4});

    const Attached attached("NoSuchScriptIsCompiledIn", vars);

    REQUIRE_FALSE(attached.component.IsResolved());
    CHECK(attached.component.Fields().Empty());

    Json out;
    REQUIRE(attached.component.Serialize(out));

    CHECK(out["script"] == "NoSuchScriptIsCompiledIn");
    REQUIRE(out.contains("vars"));
    CHECK(out["vars"] == vars);
}

TEST_CASE("the values are handed over when the script does turn up") {
    Json vars;
    vars["number"] = 42.0;

    // Attached to a name this build does not have...
    Attached attached("NoSuchScriptIsCompiledIn", vars);
    REQUIRE_FALSE(attached.component.IsResolved());

    // ...and then pointed at one it does, which is what a rebuild amounts to
    // from the component's point of view.
    attached.component.SetScriptName("TestAllTypes");

    REQUIRE(attached.component.IsResolved());
    CHECK(*attached.Field("number")->AsFloat() == doctest::Approx(42.0f));
}

// ===========================================================================
//  Values nothing is using
// ===========================================================================

TEST_CASE("a saved value with no variable behind it is reported") {
    Json vars;
    vars["number"]    = 42.0;
    vars["oldSpelling"] = 1.0;

    const Attached attached("TestAllTypes", vars);

    const std::vector<std::string> unused = attached.component.UnusedSavedValues();
    REQUIRE(unused.size() == 1);
    CHECK(unused[0] == "oldSpelling");
}

TEST_CASE("nothing is unused when every saved value has a variable") {
    Json vars;
    vars["number"] = 42.0;
    vars["count"]  = 11;

    const Attached attached("TestAllTypes", vars);
    CHECK(attached.component.UnusedSavedValues().empty());
}

TEST_CASE("switching script keeps the values rather than throwing them away") {
    Json vars;
    vars["number"] = 42.0;
    vars["blend"]  = 0.75;

    Attached attached("TestAllTypes", vars);
    REQUIRE(*attached.Field("number")->AsFloat() == doctest::Approx(42.0f));

    attached.component.SetScriptName("TestBounded");

    // The value this script DOES have a variable for is applied...
    REQUIRE(attached.Field("blend") != nullptr);
    CHECK(*attached.Field("blend")->AsFloat() == doctest::Approx(0.75f));

    // ...and the other is kept, not deleted, so switching back costs nothing.
    CHECK(attached.component.SavedValues().contains("number"));

    const std::vector<std::string> unused = attached.component.UnusedSavedValues();
    REQUIRE(unused.size() == 1);
    CHECK(unused[0] == "number");

    attached.component.SetScriptName("TestAllTypes");
    CHECK(*attached.Field("number")->AsFloat() == doctest::Approx(42.0f));
}

TEST_CASE("forgetting the unused ones leaves the rest alone") {
    Json vars;
    vars["number"]      = 42.0;
    vars["oldSpelling"] = 1.0;
    vars["alsoGone"]    = 2.0;

    Attached attached("TestAllTypes", vars);

    CHECK(attached.component.ForgetUnusedSavedValues() == 2);
    CHECK(attached.component.SavedValues().size() == 1);
    CHECK(attached.component.SavedValues().contains("number"));
    CHECK(attached.component.UnusedSavedValues().empty());
}

TEST_CASE("forgetting is REFUSED while the script is not compiled in") {
    // ======================================================================
    //  With nothing bound there are no variables, so every saved value looks
    //  unused - and this would empty the whole block, which is the exact data
    //  loss the component exists to prevent. The Inspector does not offer the
    //  button in that state either; this is the half that cannot be bypassed.
    // ======================================================================
    Json vars;
    vars["speed"] = 320.0;

    Attached attached("NoSuchScriptIsCompiledIn", vars);
    REQUIRE_FALSE(attached.component.IsResolved());
    REQUIRE(attached.component.UnusedSavedValues().size() == 1);

    CHECK(attached.component.ForgetUnusedSavedValues() == 0);
    CHECK(attached.component.SavedValues().contains("speed"));
}

// ===========================================================================
//  References to other entities
//
//  A reference stores a NAME and resolves it on every ask. Most of what can go
//  wrong is a name that does not answer, so most of these are about that.
// ===========================================================================

TEST_CASE("an unset reference refers to nothing") {
    const TestScene world;

    EntityRef ref;
    CHECK_FALSE(ref.IsSet());
    CHECK(ref.Target() == nullptr);
    CHECK(ref.Id().IsNull());
    CHECK_FALSE(static_cast<bool>(ref));
    CHECK(ref.TargetName().empty());
}

TEST_CASE("a reference finds the entity with that name") {
    TestScene world;
    Entity&   player = world.Add("Player");

    EntityRef ref;
    ref.Set("Player");

    CHECK(ref.IsSet());
    CHECK(ref.Target() == &player);
    CHECK(ref.Id() == player.Id());
    CHECK(static_cast<bool>(ref));
}

TEST_CASE("a reference can be set from the entity itself") {
    TestScene world;
    Entity&   player = world.Add("Player");

    EntityRef ref;
    ref.Set(player);

    CHECK(ref.TargetName() == "Player");
    CHECK(ref.Target() == &player);
}

TEST_CASE("a name nothing answers to resolves to null AND KEEPS THE NAME") {
    const TestScene world;

    EntityRef ref;
    ref.Set("NotHere");

    CHECK(ref.Target() == nullptr);

    // The half that matters. A reference that blanked itself would turn
    // "this points at something that is missing" into "this was never set",
    // and the Inspector would have nothing to show in red.
    CHECK(ref.IsSet());
    CHECK(ref.TargetName() == "NotHere");
}

TEST_CASE("destroying the target leaves the reference holding the name") {
    TestScene      world;
    const EntityId id = world.Add("Doomed").Id();

    EntityRef ref;
    ref.Set("Doomed");
    REQUIRE(ref.Target() != nullptr);

    world.scene.DestroyEntityImmediate(id);

    CHECK(ref.Target() == nullptr);
    CHECK(ref.TargetName() == "Doomed");
}

TEST_CASE("clearing a reference forgets the name as well") {
    TestScene world;
    world.Add("Player");

    EntityRef ref;
    ref.Set("Player");
    ref.Clear();

    CHECK_FALSE(ref.IsSet());
    CHECK(ref.Target() == nullptr);
    CHECK(ref.TargetName().empty());
}

TEST_CASE("a reference finds a component on the far entity") {
    TestScene world;
    Entity&   decorated = world.Add("Decorated");
    REQUIRE(decorated.AddComponent("SpriteComponent") != nullptr);
    world.Add("Bare");

    EntityRef ref;

    ref.Set("Decorated");
    CHECK(ref.Find<SpriteComponent>() != nullptr);

    // There, but without one.
    ref.Set("Bare");
    CHECK(ref.Find<SpriteComponent>() == nullptr);

    // Not there at all. Same answer, which is the point - a caller checks one
    // pointer rather than telling three failures apart.
    ref.Set("Gone");
    CHECK(ref.Find<SpriteComponent>() == nullptr);

    ref.Clear();
    CHECK(ref.Find<SpriteComponent>() == nullptr);
}

TEST_CASE("a reference resolves against the ACTIVE scene, whichever that is") {
    // Not a detail worth skipping: it is why a reference saved in a level file
    // means the same thing every time that level is loaded, into whatever
    // Scene object the engine happens to use that time.
    EntityRef ref;
    ref.Set("Player");

    {
        TestScene first;
        first.Add("Player");
        CHECK(ref.Target() != nullptr);
    }   // first stops being the active scene here

    CHECK(ref.Target() == nullptr);

    {
        TestScene second;
        Entity&   other = second.Add("Player");
        CHECK(ref.Target() == &other);
    }
}

// ===========================================================================
//  "Does it have one of those?" - the question a constraint asks
// ===========================================================================

TEST_CASE("an entity satisfies a constraint naming a component it has") {
    TestScene world;
    Entity&   entity = world.Add("Thing");
    REQUIRE(entity.AddComponent("SpriteComponent") != nullptr);

    CHECK(EntityHasNamed(entity, "SpriteComponent"));
    CHECK_FALSE(EntityHasNamed(entity, "Rigidbody2DComponent"));
}

TEST_CASE("an entity satisfies a constraint naming a SCRIPT it carries") {
    // ======================================================================
    //  THE CASE A PLAIN FindComponent CANNOT ANSWER.
    //
    //  Every script on every entity is a ScriptComponent, so asking for the
    //  type name "TestPlain" finds nothing however many TestPlains are
    //  attached. The name that tells one script from another is inside the
    //  component, which is why EntityHasNamed exists at all.
    // ======================================================================
    TestScene world;
    Entity&   entity = world.AddWithScript("Scripted", "TestPlain");

    CHECK(EntityHasNamed(entity, "TestPlain"));
    CHECK_FALSE(EntityHasNamed(entity, "TestBounded"));

    // And the component type is still findable, for anything that wants to
    // know "does this carry any script at all".
    CHECK(EntityHasNamed(entity, "ScriptComponent"));
}

TEST_CASE("an entity carrying SEVERAL scripts satisfies a constraint on any of them") {
    // The reason this is a walk and not a FindComponent: Entity::FindComponent
    // returns the FIRST match, so a constraint on the second script attached
    // would never be satisfied.
    TestScene world;
    Entity&   entity = world.AddWithScript("Busy", "TestPlain");

    auto    second = std::make_unique<ScriptComponent>();
    Json    node;
    node["script"] = "TestBounded";
    std::string error;
    REQUIRE(second->Deserialize(node, error));
    entity.AddComponent(std::move(second));

    CHECK(EntityHasNamed(entity, "TestPlain"));
    CHECK(EntityHasNamed(entity, "TestBounded"));
    CHECK_FALSE(EntityHasNamed(entity, "TestAllTypes"));
}

TEST_CASE("an empty constraint is satisfied by anything") {
    TestScene world;
    CHECK(EntityHasNamed(world.Add("Plain"), ""));
}

TEST_CASE("a constraint naming nothing registered is recognisable as such") {
    CHECK(IsKnownComponentOrScriptName("SpriteComponent"));   // built in
    CHECK(IsKnownComponentOrScriptName("TestPlain"));         // a script

    // What catches a tooltip passed where the required type goes.
    CHECK_FALSE(IsKnownComponentOrScriptName("what to chase"));
    CHECK_FALSE(IsKnownComponentOrScriptName(""));
}

// ===========================================================================
//  A reference as a script variable
// ===========================================================================

TEST_CASE("a reference is collected like any other variable") {
    const Attached attached("TestRefs");

    REQUIRE(attached.component.IsResolved());
    REQUIRE(attached.component.Fields().Count() == 4);

    REQUIRE(attached.Field("anything") != nullptr);
    CHECK(attached.Field("anything")->type == ScriptFieldType::Entity);
    CHECK(attached.Field("anything")->AsEntityRef() != nullptr);

    // Wrong accessors still refuse, as for every other type.
    CHECK(attached.Field("anything")->AsFloat() == nullptr);
    CHECK(attached.Field("anything")->AsString() == nullptr);
}

TEST_CASE("Ref remembers what the target has to carry, and Var leaves it open") {
    const Attached attached("TestRefs");

    CHECK(attached.Field("anything")->requiredType.empty());
    CHECK(attached.Field("needsSprite")->requiredType == "SpriteComponent");
    CHECK(attached.Field("needsScript")->requiredType == "TestPlain");

    // The tooltip is still the last argument, which is the whole reason the
    // required type is mandatory rather than defaulted.
    CHECK(attached.Field("needsScript")->tooltip == "must carry a TestPlain");
}

TEST_CASE("a reference is written to the scene as the target's name") {
    TestScene world;
    world.Add("Player");

    Attached attached("TestRefs");
    attached.Field("anything")->AsEntityRef()->Set("Player");
    REQUIRE(attached.component.SaveFieldValue("anything"));

    Json out;
    REQUIRE(attached.component.Serialize(out));
    CHECK(out["vars"]["anything"] == "Player");
}

TEST_CASE("a BROKEN reference is still written out, with the name it had") {
    // ======================================================================
    //  Writing "" for a target that has been deleted would turn a reference
    //  somebody can see and put right into one that was never set - and would
    //  do it the first time the scene was saved, which is to say immediately.
    // ======================================================================
    TestScene      world;
    const EntityId id = world.Add("Doomed").Id();

    Attached attached("TestRefs");
    attached.Field("anything")->AsEntityRef()->Set("Doomed");
    REQUIRE(attached.component.SaveFieldValue("anything"));

    world.scene.DestroyEntityImmediate(id);
    REQUIRE(attached.Field("anything")->AsEntityRef()->Target() == nullptr);

    Json out;
    REQUIRE(attached.component.Serialize(out));
    CHECK(out["vars"]["anything"] == "Doomed");
}

TEST_CASE("a reference is loaded from the scene by name") {
    TestScene world;
    Entity&   player = world.Add("Player");

    Json vars;
    vars["anything"] = "Player";

    const Attached attached("TestRefs", vars);

    REQUIRE(attached.Field("anything")->AsEntityRef() != nullptr);
    CHECK(attached.Field("anything")->AsEntityRef()->Target() == &player);
}

TEST_CASE("a reference loaded before its target exists keeps the name") {
    // The ordinary case while a scene loads: this component is built before
    // the entity it points at. Nothing resolves yet and nothing is lost.
    const TestScene world;

    Json vars;
    vars["anything"] = "ArrivesLater";

    const Attached attached("TestRefs", vars);
    const EntityRef* ref = attached.Field("anything")->AsEntityRef();

    REQUIRE(ref != nullptr);
    CHECK(ref->Target() == nullptr);
    CHECK(ref->TargetName() == "ArrivesLater");
}

TEST_CASE("a reference survives a round trip through JSON") {
    EntityRef ref;
    ref.Set("Somebody");

    const ScriptField field = Bind(ref);
    const Json        out   = FieldToJson(field);
    REQUIRE(out.is_string());
    CHECK(out.get<std::string>() == "Somebody");

    ref.Clear();
    std::string error;
    REQUIRE(ReadFieldFromJson(field, out, error));
    CHECK(ref.TargetName() == "Somebody");
}

TEST_CASE("an empty string is a reference to nothing") {
    EntityRef ref;
    ref.Set("Somebody");

    const ScriptField field = Bind(ref);
    std::string       error;

    REQUIRE(ReadFieldFromJson(field, Json(""), error));
    CHECK_FALSE(ref.IsSet());
}

TEST_CASE("a reference refuses anything that is not text") {
    EntityRef ref;
    ref.Set("Kept");

    const ScriptField field = Bind(ref);
    std::string       error;

    CHECK_FALSE(ReadFieldFromJson(field, Json(12), error));
    CHECK_FALSE(ReadFieldFromJson(field, Json::array({1, 2}), error));
    CHECK(ref.TargetName() == "Kept");
    CHECK(error.find("name") != std::string::npos);
}

TEST_CASE("reverting a reference puts the script's own target back") {
    TestScene world;
    world.Add("Player");
    world.Add("Other");

    Json vars;
    vars["anything"] = "Other";

    Attached attached("TestRefs", vars);
    REQUIRE(attached.Field("anything")->AsEntityRef()->TargetName() == "Other");

    REQUIRE(attached.component.RevertFieldValue("anything"));

    // TestRefs declares `EntityRef anything;` with no target, so that is what
    // the default is. GameRules.cpp is the other case - a reference whose
    // default IS a name, written in the code.
    CHECK_FALSE(attached.Field("anything")->AsEntityRef()->IsSet());
    CHECK_FALSE(attached.component.IsFieldOverridden("anything"));
}

// ===========================================================================
//  Renaming the far end
// ===========================================================================

TEST_CASE("renaming an entity moves every reference to it") {
    // ======================================================================
    //  THE ONE THING THAT WOULD MAKE NAME-BASED REFERENCES UNUSABLE.
    //
    //  Renaming something in the Hierarchy is an ordinary act, and without
    //  this it would quietly disconnect every script pointing at it.
    // ======================================================================
    TestScene      world;
    const EntityId id = world.Add("Player").Id();

    Json vars;
    vars["anything"] = "Player";
    Attached attached("TestRefs", vars);
    attached.component.OnAttach();   // so ScriptSystem knows about it

    REQUIRE(attached.Field("anything")->AsEntityRef()->Target() != nullptr);

    REQUIRE(world.scene.RenameEntity(id, "Hero"));

    const EntityRef* ref = attached.Field("anything")->AsEntityRef();
    CHECK(ref->TargetName() == "Hero");
    CHECK(ref->Target() != nullptr);

    // AND THE SAVED VALUE TOO. Moving the live one alone would work perfectly
    // until the level was loaded again.
    Json out;
    REQUIRE(attached.component.Serialize(out));
    CHECK(out["vars"]["anything"] == "Hero");
}

TEST_CASE("renaming leaves a reference that was never pointing anywhere alone") {
    TestScene      world;
    const EntityId id = world.Add("Player").Id();

    // `anything` has no default, so nothing refers to Player through it and
    // nothing should start to.
    Attached attached("TestRefs");
    attached.component.OnAttach();

    REQUIRE(world.scene.RenameEntity(id, "Hero"));

    CHECK_FALSE(attached.component.IsFieldOverridden("anything"));
}

TEST_CASE("renaming a reference's DEFAULT target is saved, or it would come back") {
    // ======================================================================
    //  THE BUG A MUTATION TEST FOUND, and the case that proves the fix.
    //
    //  `EntityRef preset{"Preset"}` is a reference whose default is a name in
    //  the code - how GameRules.cpp points at its HUD labels. Rename Preset
    //  and the live reference follows; if that move is not WRITTEN DOWN,
    //  nothing in the scene file disagrees with the code, and the next load
    //  puts the old name back and quietly breaks the link.
    //
    //  The earlier version of this guarded the save with "only if it was
    //  already overridden", which skipped exactly this case.
    // ======================================================================
    TestScene      world;
    const EntityId id = world.Add("Preset").Id();

    Attached attached("TestRefs");
    attached.component.OnAttach();
    REQUIRE(attached.Field("preset")->AsEntityRef()->Target() != nullptr);

    REQUIRE(world.scene.RenameEntity(id, "Renamed"));

    // The live half.
    CHECK(attached.Field("preset")->AsEntityRef()->TargetName() == "Renamed");

    // The half that survives a reload.
    CHECK(attached.component.IsFieldOverridden("preset"));
    Json out;
    REQUIRE(attached.component.Serialize(out));
    CHECK(out["vars"]["preset"] == "Renamed");

    // And nothing else was touched on the way past.
    CHECK_FALSE(out["vars"].contains("anything"));

    // The proof that it survives: a second component built from what the
    // first one wrote resolves to the renamed entity.
    ScriptComponent reloaded;
    std::string     error;
    REQUIRE(reloaded.Deserialize(out, error));
    reloaded.OnAttach();
    CHECK(reloaded.Fields().Find("preset")->AsEntityRef()->Target() != nullptr);
}

TEST_CASE("renaming leaves references to OTHER entities alone") {
    TestScene world;
    world.Add("Player");
    const EntityId other = world.Add("Other").Id();

    Json vars;
    vars["anything"] = "Player";
    Attached attached("TestRefs", vars);
    attached.component.OnAttach();

    REQUIRE(world.scene.RenameEntity(other, "Renamed"));

    CHECK(attached.Field("anything")->AsEntityRef()->TargetName() == "Player");
}

TEST_CASE("a rename refuses a name something else already has") {
    // Pre-existing behaviour, asserted here because the whole by-name scheme
    // rests on it: two entities sharing a name would make a reference
    // ambiguous and there would be no right answer.
    TestScene      world;
    const EntityId first = world.Add("Player").Id();
    world.Add("Enemy");

    CHECK_FALSE(world.scene.RenameEntity(first, "Enemy"));
    CHECK(world.scene.Get(first)->Name() == "Player");
}

TEST_CASE("a rename cannot reach a script that is not compiled in") {
    // ======================================================================
    //  THE DOCUMENTED LIMIT, asserted so it stays the behaviour rather than
    //  becoming a surprise.
    //
    //  With no script bound there are no fields, so nothing says which saved
    //  values are references. Patching every saved string that happened to
    //  equal the old name would also rewrite a `std::string label` that
    //  legitimately read "Player" - silently, in a file, as a side effect of
    //  renaming something else. The name is left for somebody to fix, with
    //  the Inspector showing it in red.
    // ======================================================================
    TestScene      world;
    const EntityId id = world.Add("Player").Id();

    Json vars;
    vars["anything"] = "Player";
    Attached attached("NoSuchScriptIsCompiledIn", vars);
    attached.component.OnAttach();
    REQUIRE_FALSE(attached.component.IsResolved());

    REQUIRE(world.scene.RenameEntity(id, "Hero"));

    Json out;
    REQUIRE(attached.component.Serialize(out));
    CHECK(out["vars"]["anything"] == "Player");   // not moved, not lost
}

// ===========================================================================
//  Reaching the script on the far side
// ===========================================================================

TEST_CASE("a script on another entity can be found by its type") {
    TestScene world;
    Entity&   entity = world.AddWithScript("Scripted", "TestPlain");

    // TestPlain is nameable here because this test file declares it. A script
    // under assets/scripts is in an anonymous namespace and is not - which is
    // why a reference is usually constrained by NAME instead. See the note in
    // ScriptComponent.h.
    CHECK(FindScriptOn<TestPlain>(entity) != nullptr);
    CHECK(FindScriptOn<TestBounded>(entity) == nullptr);
}

TEST_CASE("a script is found through a reference") {
    TestScene world;
    world.AddWithScript("Scripted", "TestPlain");

    EntityRef ref;
    ref.Set("Scripted");
    CHECK(FindScriptOn<TestPlain>(ref) != nullptr);

    ref.Set("Nothing");
    CHECK(FindScriptOn<TestPlain>(ref) == nullptr);

    ref.Clear();
    CHECK(FindScriptOn<TestPlain>(ref) == nullptr);
}

TEST_CASE("a script can be found by the name the registry knows it under") {
    TestScene world;
    Entity&   entity = world.AddWithScript("Scripted", "TestBounded");

    REQUIRE(FindScriptNamedOn(entity, "TestBounded") != nullptr);
    CHECK(FindScriptNamedOn(entity, "TestBounded")->ScriptName() == "TestBounded");
    CHECK(FindScriptNamedOn(entity, "TestPlain") == nullptr);
    CHECK(FindScriptNamedOn(entity, "") == nullptr);
}

TEST_CASE("the far script's own variables are reachable through it") {
    // What a typed reference is FOR: reading and writing the other script's
    // members, not just knowing it is there.
    TestScene world;
    Entity&   entity = world.AddWithScript("Bounded", "TestBounded");

    TestBounded* script = FindScriptOn<TestBounded>(entity);
    REQUIRE(script != nullptr);
    CHECK(script->lives == 3);

    script->lives = 1;
    CHECK(FindScriptOn<TestBounded>(entity)->lives == 1);
}

// ===========================================================================
//  The whole trip: a scene file, out and back
// ===========================================================================

TEST_CASE("a component written out and read back in behaves identically") {
    Json vars;
    vars["number"]  = 9.25;
    vars["enabled"] = false;
    vars["offset"]  = Json::array({100.0, 200.0});

    Json saved;
    {
        const Attached first("TestAllTypes", vars);
        REQUIRE(first.component.Serialize(saved));
    }

    // A second component, built from nothing but what the first one wrote.
    ScriptComponent second;
    std::string     error;
    REQUIRE(second.Deserialize(saved, error));
    second.OnAttach();

    REQUIRE(second.IsResolved());
    CHECK(*second.Fields().Find("number")->AsFloat() == doctest::Approx(9.25f));
    CHECK_FALSE(*second.Fields().Find("enabled")->AsBool());
    CHECK(second.Fields().Find("offset")->AsVec2()->y == doctest::Approx(200.0f));

    // The untouched ones are still the code's, and still absent from the file.
    CHECK(*second.Fields().Find("count")->AsInt() == 7);
    CHECK_FALSE(second.IsFieldOverridden("count"));
}
