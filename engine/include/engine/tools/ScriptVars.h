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
//      script from compiling. That guard has to ask about the variable's TYPE
//      as well as the call, as a nested requirement rather than an `&&`, and
//      GenerateExposerUnit says at length why both of those matter.
//    * The five names ScriptBehaviour itself defines - transform, gameObject,
//      scene, name and tag - are never exposed, because a member of the
//      script's own by one of those names hides the inherited one.
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

    // THE MARKER'S NAME, which means one of two things depending on the type
    // it lands on: [Rigidbody2DComponent] on an EntityRef narrows the picker
    // to entities carrying one, and [animation] or [image] on a std::string
    // says the text is an asset's path and gets it a slot instead of a box.
    //
    // Dropped for every other type - a marker on a float means nothing and
    // carrying it would leave something downstream to wonder about.
    //
    // THE MARKER MAY BE ON THE DECLARATION OR ON THE COMMENT LINE DIRECTLY
    // ABOVE IT. See ReadComment in ScriptVars.cpp for why both, and why not
    // any further above than that.
    //
    // See ScriptFields::Ref and ScriptFields::Asset.
    std::string requiredType;

    // ======================================================================
    //  A FIXED-SIZE ARRAY, exposed as one row PER ELEMENT.
    //
    //      std::string walkAnimations[4];
    //
    //  appears as walkAnimations[0] .. walkAnimations[3], four ordinary
    //  variables of the element's type.
    //
    //  WHY THAT AND NOT A LIST WIDGET. Every piece of machinery behind a
    //  script variable - the type table, the Inspector's widgets, the scene
    //  file, prefab overrides, the undo stack - works on "a name and a value".
    //  Give each element its own name and all of it applies unchanged, with
    //  nothing to teach any of those five things about lists. The exposer
    //  writes a loop over std::size(), so the LENGTH never has to be parsed
    //  out of the declaration and may be a named constant.
    //
    //  A MARKER APPLIES TO EVERY ELEMENT, and that is what makes an array of
    //  asset names worth having. `// [animation]` on the declaration gives all
    //  four elements the animation slot rather than four boxes to type a path
    //  into; `// [0..10]` gives all four a slider. There is nothing
    //  per-element to write, and nothing anywhere about a particular
    //  variable's name. See eng::AssetFieldKind in scene/ScriptFields.h.
    //
    //  WHAT IS STILL NOT SUPPORTED, and the distinction is worth keeping: a
    //  GROWABLE list. A std::vector has no length until the game runs, so
    //  there is nothing for the Inspector to lay out and nothing for the
    //  scene file to have a key for. A fixed array has both. "Four
    //  animations, one per direction" is a shape; "some waypoints" is not.
    // ======================================================================
    bool isArray = false;
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

// ===========================================================================
//  THE SAME SCAN, TOLD WHICH NAMES ARE SCRIPTS.
//
//  `Door door;` is the spelling a reference to another script wants, because
//  it is the spelling Unity has and the one somebody writes without being
//  taught. But a bare identifier is the one declaration this file cannot
//  judge alone: `Door door;` and `Matrix4 transform;` look identical, and
//  only the project knows which of the two names a script registers.
//
//  SO THE NAMES ARE PASSED IN, as data, and the scan stays pure - no
//  registry, no engine state, no file system, the property the note at the
//  top of this header rests on. ScriptBuild already computes exactly this
//  list (GatherScriptClasses) before it generates anything.
//
//  A name in `projectScripts` makes `Door door;` an entity reference whose
//  required type is "Door". With an empty list the two overloads agree
//  exactly, which is why the one above is still here: every caller that does
//  not have a project behind it - the tests, a lone file - keeps working.
// ===========================================================================
ScriptSourceInfo ScanScriptSource(std::string_view               source,
                                  const std::vector<std::string>& projectScripts);

// ===========================================================================
//  FOUR MORE QUESTIONS ABOUT A SCRIPT'S TEXT, and they are here rather than
//  in the editor for the reason the top of this file gives: they are pure -
//  text in, answer out, no files, no compiler, no engine state - so they can
//  be tested without building a project, and they are the part of the build
//  with the most ways to be subtly wrong.
//
//  THE SAME WHITELIST CAUTION APPLIES TO ALL OF THEM. Each can be fooled by
//  text that merely looks like what it is searching for, so no caller is
//  allowed to REFUSE a build over an answer: they shape generated code, or
//  they offer an explanation, and the compiler stays the authority.
// ===========================================================================

// The same text with every comment replaced by spaces - and nothing else
// touched, so offsets and line numbers still line up with the original.
//
// STRING AND CHARACTER LITERALS ARE TRACKED, only so that a `//` inside one -
// a URL, a Windows path - is not mistaken for the start of a comment.
//
// Every structural question below should be asked of this rather than of the
// source, because a file is allowed to EXPLAIN the code it contains: a note
// saying that `other->Method()` is what a mutual pair cannot do must not be
// read as that script doing it. ScanScriptSource is the exception and must
// not use it - tooltips and markers are read from the comments.
std::string StripComments(std::string_view source);

// `class` or `struct`, whichever the author used to define that class, and
// "class" when the definition cannot be found. Needed because every script is
// forward-declared before any of them is defined, and a declaration that
// disagrees with its definition earns warning C4099 about a generated line.
std::string ClassKeyOf(std::string_view code, std::string_view className);

// Does this text reach THROUGH that member - `doorSwitch->Open()` - rather
// than merely holding it?
//
// THE QUESTION THE WRAPPING ORDER TURNS ON. Declaring a reference, testing it
// with `if (ref)`, exposing it and assigning it all go through ScriptRef's own
// members, which are members of a TEMPLATE and are compiled once the whole
// unit has been read - so a forward declaration is enough and the order does
// not matter. `ref->Method()` is ordinary member lookup in the other class and
// needs it FINISHED, so the caller has to be wrapped after it.
//
// `ref . Member` is deliberately NOT this: that reaches ScriptRef itself.
bool ReachesThroughReference(std::string_view code, std::string_view member);

// One script named in another's base-clause.
struct ScriptBaseUse {
    std::string name;

    // Written `Door::ScriptType`, which is the spelling that works. A bare
    // script name there asks to inherit from a REFERENCE, because that is what
    // the bare name is; ScriptRef declares ScriptType for exactly this case.
    bool qualified = false;
};

// Which of `projectScripts` does className derive from? Both for the order -
// a base class has to be complete, so it must be wrapped first - and so that
// somebody who wrote the bare name can be told what to write instead.
std::vector<ScriptBaseUse> BaseScriptsOf(std::string_view code,
                                         std::string_view className,
                                         const std::vector<std::string>& projectScripts);

// One script's contribution to a generated unit: its class, where it really
// lives, and its text. GenerateScriptUnit takes several of these.
struct ScriptUnitSource {
    std::string className;   // "Door", from its ENGINE_REGISTER_SCRIPT
    std::string realPath;    // absolute, forward slashes - for #line
    std::string text;        // the file as the author wrote it

    // `class` OR `struct`, AS THE AUTHOR WROTE IT, because every script is
    // FORWARD-DECLARED before any of them is defined and the two have to
    // agree. Declaring `class Door;` for a `struct Door` is legal and gets
    // warning C4099 for it - noise in a generated file about a difference the
    // author cannot see. Read off the declaration by ClassKeyOf in
    // ScriptBuild.cpp; "class" is the right default for anything unreadable,
    // because that is what a script written from the templates says.
    std::string classKey = "class";
};

// ===========================================================================
//  THE UNIT FOR A SCRIPT THAT REFERS TO OTHER SCRIPTS.
//
//  `sources` is every script in the project. EVERY ONE OF THEM IS DECLARED
//  AND NAMED BEFORE ANY OF THEM IS DEFINED, and then each is wrapped in a
//  namespace of its own:
//
//      namespace s_Door       { class Door; }
//      namespace s_DoorSwitch { class DoorSwitch; }
//
//      using Door       = ::eng::ScriptRef<s_Door::Door>;
//      using DoorSwitch = ::eng::ScriptRef<s_DoorSwitch::DoorSwitch>;
//
//      namespace s_Door {
//      using DoorSwitch = ::eng::ScriptRef<s_DoorSwitch::DoorSwitch>;
//      #line 1 "C:/.../Door.cpp"
//      ... Door.cpp, minus its #include lines ...
//      }
//
//  INSIDE s_Door THE BARE NAME IS THE CLASS, so Door.cpp compiles exactly as
//  written and ENGINE_REGISTER_SCRIPT(Door) still names the class. ANYWHERE
//  ELSE THE SAME NAME IS A HANDLE, so every other script sees `Door door;` as
//  a reference.
//
//  WHY THE DECLARATIONS AND THE ALIASES COME FIRST, which is the whole of
//  what makes this general: a reference does not need the class it points at
//  to be COMPLETE. ScriptRef<T> keeps a NAME, inherited from EntityRef, so
//  its size and layout are the same whatever T is, and the only thing that
//  needs T whole is Get() - instantiated where the reference is USED, which
//  is why ScriptRef.h puts its static_assert in there rather than in the
//  class body.
//
//  So every script can name every other one, in any direction, and two that
//  point at each other are no harder than two that do not. Emitting the alias
//  after each namespace instead - which is what this did first - quietly
//  meant "only the ones wrapped earlier", and a pair of scripts that referred
//  to each other cannot be ordered so that both come first. That failed as
//  `error C3646: 'doorSwitch': unknown override specifier` on the author's
//  own line, with nothing in the message to connect it to an ordering rule it
//  never knew about.
//
//  WHY THE NAMESPACE IS THE SAME IN EVERY UNIT, including the script's own:
//  resolving a reference ends in dynamic_cast, and MSVC matches type_info by
//  DECORATED NAME. ::Door and s_Door::Door are different names, so if one
//  library wrapped the class and another did not, the cast would return null
//  with nothing to see. Wrapping everywhere makes every library agree.
//
//  WHY THE #include LINES COME OUT: an #include inside a namespace puts
//  whatever it names inside that namespace too, and assets/scripts has real
//  cases - Orbiter.cpp includes <cmath>. They are hoisted above every
//  namespace and de-duplicated. Each one leaves a BLANK LINE behind so the
//  line numbering never shifts and the #line above stays true.
//
//  It copies rather than includes, which GenerateExposerUnit deliberately
//  does not - so #line carries what the include used to: a compiler error
//  points at the author's file and line, and __FILE__ is still the real
//  path, which ScriptRegistry::Register compares registrations by.
// ===========================================================================
//  `unitPath` is where this generated file will be written, so that the #line
//  directive after each script's text can point back at it. Without that, the
//  closing brace and the alias are attributed to the author's last line and a
//  diagnostic about them accuses a file that does not contain them.
//
//  An exposer for `info` is appended only when there is something to expose -
//  a script with a hand-written OnExpose must not be given a generated one as
//  well, because the hand-written list is the one that wins.
std::string GenerateScriptUnit(const ScriptSourceInfo&              info,
                               const std::vector<ScriptUnitSource>& sources,
                               std::string_view                     unitPath);

// ===========================================================================
//  THE SAME SCOPE, FOR SOMETHING THAT READS ONE SCRIPT AT A TIME.
//
//  AN IDE DOES NOT SEE THE GENERATED UNIT. It opens the author's .cpp on its
//  own, where another script's bare name has never been declared - so
//  `Bullet b;` is underlined as an unknown type, `b->Fire()` has no
//  completion, and the code the editor compiles without complaint looks
//  broken in the window it was written in. That is worse than it sounds: a
//  beginner believes the IDE.
//
//  So the editor writes this header and tells the IDE to force-include it
//  (editor/src/ScriptIde.cpp, forcedInclude / /FI). It declares every script
//  in the project, defines every one of them inside its own namespace so that
//  members resolve, and aliases the bare names.
//
//  WHY THE ALIASES GO IN A NAMESPACE HERE AND AT GLOBAL SCOPE THERE, which is
//  the only real difference from GenerateScriptUnit: a forced include applies
//  to EVERY file equally - Visual Studio's CppProperties.json has one list for
//  the whole configuration, with no way to say "except in Bullet.cpp". So the
//  header cannot leave out the alias for the file being edited, and that file
//  defines a class of exactly that name at global scope. Two declarations of
//  one name in one scope is a redefinition; a declaration plus a name brought
//  in by a using-directive is not, and unqualified lookup only complains if
//  something actually uses the name ambiguously. A script does that exactly
//  once - its ENGINE_REGISTER_SCRIPT line - which is why this header ends by
//  redefining that macro to the qualified form. See ENGINE_SCRIPT_CHECKS in
//  scene/ScriptHooks.h.
//
//  WHAT IT IS DELIBERATELY NOT: a description of the real compile. It is more
//  permissive in one way - every script is complete by the time the author's
//  file is read, so a loop of calls between two scripts shows no error here
//  and is still refused by the build, which names it. And less permissive in
//  another - a script's own file is at global scope rather than inside its
//  namespace, so the file-scope helpers of other scripts are not in scope
//  while you type even though they compile. Both are stated in the header's
//  own text, because a generated file that lies about itself is worse than
//  one that is merely incomplete.
//
//  `sources` is every script in the project, in the same dependency order
//  GenerateScriptUnit takes.
// ===========================================================================
std::string GenerateScriptScopeHeader(const std::vector<ScriptUnitSource>& sources);

// Just the exposer registration for one script, with `castTarget` spelling the
// class as THIS unit can reach it - the bare name when the source was included
// at global scope, s_Name::Name when GenerateScriptUnit wrapped it.
//
// Separate from the two above because one unit can carry several scripts: the
// generated unit for scripts written in headers wraps them all once and then
// needs one of these per script.
std::string GenerateExposerFor(const ScriptSourceInfo& info,
                               std::string_view        castTarget);

// The C++ this turns into: a translation unit that includes the original and
// then registers an exposer for it.
//
// INCLUDES THE ORIGINAL RATHER THAN COPYING IT, and that is the whole trick.
// Line numbers, __FILE__ and the file's own `static` helpers all stay exactly
// as they were, so a compiler error still points at the line you wrote and
// ScriptRegistry still records the real source path. A rewritten copy would
// have needed #line directives to manage half of that and would have got the
// other half wrong.
//
// __FILE__ IS LOAD-BEARING AND NOT MERELY TIDY: it is what
// ScriptRegistry::Register compares two registrations of one name by, which is
// how a script class shared through a header registers once rather than
// reporting a clash with itself.
//
// `includePath` is the original source, as the generated file should spell it.
std::string GenerateExposerUnit(const ScriptSourceInfo& info,
                                std::string_view includePath);

} // namespace eng
