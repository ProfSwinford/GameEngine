#pragma once

// ============================================================================
//  ScriptVars.h - finding a script's variables by READING THE SOURCE.
//
//  This is what lets a script say nothing at all and still have its variables
//  in the Inspector:
//
//      class Orbiter final : public eng::ScriptBehaviour {
//      public:
//          float radius = 90.0f;             // how far out it orbits
//          float speed  = 1.2f;              // [0..10] turns per second
//          eng::EntityRef centre;            // [TransformComponent] what to circle
//
//          void OnFixedUpdate() { ... }
//      };
//      ENGINE_REGISTER_SCRIPT(Orbiter)
//
//  No OnExpose, no macro per variable, no annotation. Public, a supported
//  type, and it is on screen - which is how Unity behaves and what this was
//  asked to match.
//
//  ==========================================================================
//  WHY THIS IS A PARSER AND NOT REFLECTION
//
//  Because C++ has no reflection, in any released standard, and the gap is
//  not a small one. ScriptHooks.h can ask "does this class have an OnUpdate?"
//  because OnUpdate is SPELLED OUT in the question. There is no way to write
//  "list the members of T" - so a float called `radius` is invisible to the
//  compiler until something names it.
//
//  Three ways out of that, and only one of them is any good here:
//
//    A MACRO PER VARIABLE - ENGINE_VAR(float, radius) - which works, and
//    which is what was already possible by hand. It is not what was asked
//    for: the variable stops being a plain float, and you still annotate
//    every one.
//
//    A DIFFERENT LANGUAGE. C# has reflection and Unity uses it. It also needs
//    a runtime embedded, and a binding for every engine function a script can
//    call - permanently, for every function added afterwards. That is a far
//    larger change than this, to solve one narrow problem.
//
//    READING THE SOURCE, which is what Unreal does (UnrealHeaderTool) and
//    what Qt does (moc). The editor already shells out to a compiler, so
//    there is already a build step to do it in.
//
//  ==========================================================================
//  IT IS A WHITELIST, NOT A C++ PARSER, AND THAT IS THE SAFETY PROPERTY
//
//  It recognises a short list of declaration shapes and ignores everything
//  else. It does not understand templates, nested classes, macros that expand
//  to declarations, or any of the hundred other things a C++ parser has to.
//
//  So it WILL miss things, and the design makes missing them cheap:
//
//    * Anything not recognised is simply not exposed - the same as before
//      this existed. Nothing breaks.
//    * The generated code wraps every variable in `if constexpr (requires
//      ...)`, so a variable this parser thought it saw and got wrong COMPILES
//      AWAY instead of failing the build. Generated code can never stop a
//      script from compiling.
//    * What it skipped is reported, with the reason - see `skipped` below.
//      "My variable did not appear" is a question the Console answers.
//
//  That last one matters most. A silent omission is the failure this engine
//  works hardest to avoid everywhere else, and an automatic feature is
//  exactly where one would hide.
// ============================================================================

#include <engine/scene/ScriptFields.h>

#include <string>
#include <string_view>
#include <vector>

namespace eng {

// ---------------------------------------------------------------------------
//  One variable the parser found.
// ---------------------------------------------------------------------------
struct ScriptVarDecl {
    std::string name;   // the member's own name, which is also the label
    std::string type;   // as written: "float", "eng::Vec2", "Vector2"

    ScriptFieldType fieldType = ScriptFieldType::Float;

    // From the trailing comment on the same line, which is where this project
    // already explains things:
    //
    //     float speed = 1.2f;   // [0..10] how fast it turns
    //
    // A bracket group AT THE START is a marker; the rest is the tooltip.
    // Only at the start, so ordinary prose mentioning "[1]" is left alone.
    std::string tooltip;

    // [0..10] on a float or an int - drawn as a slider.
    bool  ranged = false;
    float min    = 0.0f;
    float max    = 0.0f;

    // [Rigidbody2DComponent] on an EntityRef - the picker narrows to entities
    // carrying one. See ScriptFields::Ref.
    std::string requiredType;
};

// A public member that LOOKS like it was meant to be exposed and was not, with
// the reason in plain words. Reported so that nothing is silently absent.
struct ScriptVarSkipped {
    std::string name;
    std::string reason;
};

// ---------------------------------------------------------------------------
//  What one script source turned out to contain.
// ---------------------------------------------------------------------------
struct ScriptSourceInfo {
    // From ENGINE_REGISTER_SCRIPT(...), which is the only place a script's
    // name is written down. Empty when the file registers nothing, which is
    // how a helper header is told from a script.
    std::string className;

    // True when the class writes its own OnExpose. THE HAND-WRITTEN ONE WINS:
    // somebody who went to the trouble of writing that function has said
    // exactly what they want, and a guess from the source should not argue.
    bool hasOnExpose = false;

    std::vector<ScriptVarDecl>    vars;
    std::vector<ScriptVarSkipped> skipped;
};

// Reads a script's text. Pure: no files, no compiler, no engine state - which
// is what makes it testable without building anything.
ScriptSourceInfo ScanScriptSource(std::string_view source);

// The C++ this turns into: a translation unit that includes the original and
// then registers an exposer for it.
//
// INCLUDES THE ORIGINAL RATHER THAN COPYING IT, and that is the whole trick.
// Line numbers, __FILE__ and the file's anonymous namespace all stay exactly
// as they were, so a compiler error still points at the line you wrote and
// ScriptRegistry still records the real source path. A rewritten copy would
// have needed #line directives to manage half of that and would have got the
// other half wrong.
//
// `includePath` is the original source, as the generated file should spell it.
std::string GenerateExposerUnit(const ScriptSourceInfo& info,
                                std::string_view includePath);

} // namespace eng
