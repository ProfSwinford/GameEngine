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
