// ============================================================================
//  ScriptVars.cpp - reading a script's variables out of its source text.
//
//  See ScriptVars.h for why this is a parser at all, and why being a whitelist
//  rather than a C++ parser is the safety property rather than a shortcut.
// ============================================================================

#include <engine/tools/ScriptVars.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>

namespace eng {
namespace {

// ---------------------------------------------------------------------------
//  Step one: the source, with comments taken out and kept.
//
//  Comments have to be REMOVED so that the word "ENGINE_REGISTER_SCRIPT" in a
//  file's explanatory banner - which every script generated from the template
//  has - is not mistaken for the real thing. Every script in this project
//  would otherwise be read wrong.
//
//  And they have to be KEPT, because the trailing comment on a declaration is
//  where its tooltip comes from. So each line carries both halves.
// ---------------------------------------------------------------------------
struct SourceLine {
    std::string code;      // the line with comments stripped
    std::string comment;   // its trailing // comment, if it had one
};

std::string TrimmedCopy(std::string_view text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(first, last - first + 1));
}

// A small state machine over the characters. String and character literals are
// tracked for one reason: a brace or a "//" inside one would otherwise be read
// as code, and the brace counting below depends on not being lied to.
//
// RAW STRING LITERALS - R"(...)" - are NOT understood. A script containing one
// with unbalanced braces in it could confuse the brace count, and the worst
// outcome is a variable missed or imagined. An imagined one compiles away; see
// the `requires` guard in GenerateExposerUnit.
std::vector<SourceLine> SplitSource(std::string_view source) {
    std::vector<SourceLine> lines;
    SourceLine              current;

    enum class State { Code, Block, Str, Chr };
    State state = State::Code;

    for (std::size_t i = 0; i < source.size(); ++i) {
        const char c    = source[i];
        const char next = (i + 1 < source.size()) ? source[i + 1] : '\0';

        if (c == '\n') {
            current.code    = TrimmedCopy(current.code);
            current.comment = TrimmedCopy(current.comment);
            lines.push_back(std::move(current));
            current = SourceLine{};
            // A block comment carries on across the newline; everything else
            // ends at it.
            if (state != State::Block) {
                state = State::Code;
            }
            continue;
        }

        switch (state) {
            case State::Code:
                if (c == '/' && next == '/') {
                    // The rest of the line is the comment, with the two
                    // slashes dropped. The cursor is left one before the
                    // newline so the next turn of the loop ends the line
                    // through the ordinary path.
                    const std::size_t lineEnd = source.find('\n', i);
                    const std::size_t start   = std::min(i + 2, source.size());
                    const std::size_t count   = (lineEnd == std::string_view::npos)
                                                    ? source.size() - start
                                                    : lineEnd - start;
                    current.comment.assign(source.substr(start, count));
                    i = (lineEnd == std::string_view::npos) ? source.size() : lineEnd - 1;
                    break;
                }
                if (c == '/' && next == '*') {
                    state = State::Block;
                    ++i;
                    break;
                }
                current.code += c;
                if (c == '"') {
                    state = State::Str;
                } else if (c == '\'') {
                    state = State::Chr;
                }
                break;

            case State::Block:
                if (c == '*' && next == '/') {
                    state = State::Code;
                    ++i;
                }
                break;

            case State::Str:
            case State::Chr:
                // ==========================================================
                //  THE CONTENTS ARE DROPPED, not copied, and only the closing
                //  quote is kept - so `std::string opener = "{{{";` becomes
                //  `std::string opener = "";`.
                //
                //  Found by a test. Copying them through meant a brace or a
                //  semicolon inside a literal was counted as code: three
                //  braces in a string put every later member three levels
                //  deep, and every one of them vanished from the Inspector.
                //
                //  Nothing downstream wants what was in the literal. The
                //  SHAPE is all that is read - that an initialiser follows -
                //  and the empty quotes keep that intact.
                // ==========================================================
                if (c == '\\') {
                    // An escape: step over the next character without looking
                    // at it, so \" does not end the literal.
                    if (next != '\0') {
                        ++i;
                    }
                } else if ((state == State::Str && c == '"') ||
                           (state == State::Chr && c == '\'')) {
                    current.code += c;
                    state = State::Code;
                }
                break;
        }
    }

    current.code    = TrimmedCopy(current.code);
    current.comment = TrimmedCopy(current.comment);
    lines.push_back(std::move(current));
    return lines;
}

// ---------------------------------------------------------------------------
//  Identifiers and whole-word matching
// ---------------------------------------------------------------------------

bool IsIdentChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '_';
}

// Is `needle` at this position, AND not part of a longer word? Without the
// second half, "class" would match inside "subclass" and ":: Vec2" inside
// "MyVec2".
bool WordAt(const std::string& text, std::size_t at, std::string_view needle) {
    if (text.compare(at, needle.size(), needle) != 0) {
        return false;
    }
    if (at > 0 && IsIdentChar(text[at - 1])) {
        return false;
    }
    const std::size_t after = at + needle.size();
    return after >= text.size() || !IsIdentChar(text[after]);
}

std::size_t FindWord(const std::string& text, std::string_view needle,
                     std::size_t from = 0) {
    for (std::size_t i = from; i + needle.size() <= text.size(); ++i) {
        if (WordAt(text, i, needle)) {
            return i;
        }
    }
    return std::string::npos;
}

// ---------------------------------------------------------------------------
//  The types the Inspector can draw.
//
//  SPELLED SEVERAL WAYS ON PURPOSE. A script may write eng::Vec2, or Vec2
//  after `using namespace eng`, or Vector2 - which is the typedef Engine.h
//  offers. All three mean the same thing and all three have to be recognised,
//  because a variable that is not recognised is a variable that silently does
//  not appear.
//
//  THE LONGEST SPELLING MUST BE TRIED FIRST: "eng::Vec2" starts with no other
//  entry, but "Color" is a suffix of "eng::Color", and matching the short one
//  first would leave "eng::" dangling in front of the name.
// ---------------------------------------------------------------------------
struct TypeSpelling {
    std::string_view spelling;
    ScriptFieldType  type;

    // For a COMPONENT REFERENCE alias, the component it is a reference to.
    //
    // `Transform anchor;` has to narrow the Inspector's picker to entities
    // carrying a TransformComponent, and nothing in the declaration says so -
    // the alias hides it, which is the point of the alias. Empty for every
    // other spelling, where a constraint either comes from a `// [Type]`
    // marker or does not exist.
    std::string_view required{};
};

constexpr std::array kTypeSpellings{
    // ---- the component-reference aliases from Engine.h, which are
    //      REFERENCES here as they are reference types in C#. See
    //      scene/ComponentRef.h.
    //
    // EVERY CONCRETE COMPONENT HAS ONE, and it has to be listed here as well
    // as in Engine.h: an alias leaves no `ComponentRef<...>` text for the
    // long-spelling branch below to read, so the constraint cannot be lifted
    // out of the declaration and has to be written down. A name added to
    // Engine.h and forgotten here is exposed with NO constraint, so the
    // Inspector's picker offers every entity in the scene.
    //
    // THE TWO THAT ARE DELIBERATELY ABSENT: `Transform` is impossible as a
    // field type - ScriptBehaviour::Transform() hides it - and Collider2D is
    // a pointer to an abstract base that Entity::Find cannot look up.
    // Engine.h argues both at length, and CameraRef's name as well.
    TypeSpelling{"TransformRef", ScriptFieldType::Entity, "TransformComponent"},
    TypeSpelling{"SpriteRenderer", ScriptFieldType::Entity, "SpriteComponent"},
    TypeSpelling{"Rigidbody2D", ScriptFieldType::Entity, "Rigidbody2DComponent"},
    TypeSpelling{"SpriteAnimator", ScriptFieldType::Entity, "SpriteAnimationComponent"},
    TypeSpelling{"CameraRef", ScriptFieldType::Entity, "CameraComponent"},
    TypeSpelling{"Tilemap", ScriptFieldType::Entity, "TilemapComponent"},
    TypeSpelling{"SpinRef", ScriptFieldType::Entity, "SpinComponent"},
    TypeSpelling{"BoxCollider2D", ScriptFieldType::Entity, "BoxColliderComponent"},
    TypeSpelling{"CircleCollider2D", ScriptFieldType::Entity, "CircleColliderComponent"},
    TypeSpelling{"CapsuleCollider2D", ScriptFieldType::Entity, "CapsuleColliderComponent"},
    TypeSpelling{"EdgeCollider2D", ScriptFieldType::Entity, "EdgeColliderComponent"},
    TypeSpelling{"PolygonCollider2D", ScriptFieldType::Entity, "PolygonColliderComponent"},
    TypeSpelling{"TilemapCollider2D", ScriptFieldType::Entity, "TilemapColliderComponent"},
    TypeSpelling{"UIRectRef", ScriptFieldType::Entity, "UIRectComponent"},
    TypeSpelling{"UIImageRef", ScriptFieldType::Entity, "UIImageComponent"},
    TypeSpelling{"UITextRef", ScriptFieldType::Entity, "UITextComponent"},
    TypeSpelling{"UIButtonRef", ScriptFieldType::Entity, "UIButtonComponent"},
    TypeSpelling{"UISliderRef", ScriptFieldType::Entity, "UISliderComponent"},

    // "PlayerInput" IS A PREFIX OF "PlayerInputComponent", which is in the
    // block below and is therefore tried SECOND - the one place this array's
    // own "longest spelling first" rule is not visibly obeyed. It is safe,
    // and not by luck: WordAt refuses a match whose next character is an
    // identifier character, so `PlayerInputComponent p;` cannot match the
    // short entry at all. Said out loud because the next person to reorder
    // these two blocks needs to know it is load-bearing.
    TypeSpelling{"PlayerInput", ScriptFieldType::Entity, "PlayerInputComponent"},

    // ---- and the components' OWN names, which are references too -------
    //
    // `SpriteComponent renderer;` is a slot you drag an entity onto, exactly
    // as `Invisibility invis;` is for a script - see scene/ComponentAliases.h
    // for the alias that makes it so. Without these entries the scan would
    // still read the declaration as a component held BY VALUE and refuse to
    // draw it, which is the state this pair of lists exists to prevent.
    //
    // The QUALIFIED spelling is deliberately absent: `eng::SpriteComponent x;`
    // really is the class by value, and the by-value diagnostic should still
    // fire for it.
    TypeSpelling{"TransformComponent", ScriptFieldType::Entity, "TransformComponent"},
    TypeSpelling{"SpriteComponent", ScriptFieldType::Entity, "SpriteComponent"},
    TypeSpelling{"SpriteAnimationComponent", ScriptFieldType::Entity,
                 "SpriteAnimationComponent"},
    TypeSpelling{"Rigidbody2DComponent", ScriptFieldType::Entity, "Rigidbody2DComponent"},
    TypeSpelling{"CameraComponent", ScriptFieldType::Entity, "CameraComponent"},
    TypeSpelling{"TilemapComponent", ScriptFieldType::Entity, "TilemapComponent"},
    TypeSpelling{"TilemapColliderComponent", ScriptFieldType::Entity,
                 "TilemapColliderComponent"},
    TypeSpelling{"SpinComponent", ScriptFieldType::Entity, "SpinComponent"},
    TypeSpelling{"PlayerInputComponent", ScriptFieldType::Entity,
                 "PlayerInputComponent"},
    // ScriptComponent is NOT here, and scene/ComponentAliases.h says why:
    // a reference to "whichever script is on that entity" is not a useful
    // question, and the spelling for a particular one is that script's own
    // bare name. Held by value it is an ordinary mistake and still reported.
    TypeSpelling{"BoxColliderComponent", ScriptFieldType::Entity, "BoxColliderComponent"},
    TypeSpelling{"CircleColliderComponent", ScriptFieldType::Entity,
                 "CircleColliderComponent"},
    TypeSpelling{"CapsuleColliderComponent", ScriptFieldType::Entity,
                 "CapsuleColliderComponent"},
    TypeSpelling{"EdgeColliderComponent", ScriptFieldType::Entity,
                 "EdgeColliderComponent"},
    TypeSpelling{"PolygonColliderComponent", ScriptFieldType::Entity,
                 "PolygonColliderComponent"},
    TypeSpelling{"UIRectComponent", ScriptFieldType::Entity, "UIRectComponent"},
    TypeSpelling{"UIImageComponent", ScriptFieldType::Entity, "UIImageComponent"},
    TypeSpelling{"UITextComponent", ScriptFieldType::Entity, "UITextComponent"},
    TypeSpelling{"UIButtonComponent", ScriptFieldType::Entity, "UIButtonComponent"},
    TypeSpelling{"UISliderComponent", ScriptFieldType::Entity, "UISliderComponent"},

    TypeSpelling{"eng::EntityRef", ScriptFieldType::Entity},
    TypeSpelling{"EntityRef", ScriptFieldType::Entity},
    TypeSpelling{"eng::PrefabRef", ScriptFieldType::Prefab},
    TypeSpelling{"PrefabRef", ScriptFieldType::Prefab},

    // ---- the one that can be either --------------------------------------
    //
    // "GameObject" IS AN ALIAS, like the component names above and for the
    // same reason it has to be listed here: an alias leaves no
    // `GameObjectRef` text for anything else to read.
    //
    // IT USED TO BE A POINTER TYPEDEF, which is why it was never in this
    // table - and why `GameObject thing;` was reported as a type the
    // Inspector cannot draw, with a list of scalars to pick from instead.
    // The type is a value now; see scene/GameObjectRef.h.
    //
    // NO requiredType, deliberately. The whole point is that it accepts
    // either a scene object or a prefab, so constraining it to entities
    // carrying some component would rule out half of what it is for.
    TypeSpelling{"eng::GameObjectRef", ScriptFieldType::GameObject},
    TypeSpelling{"GameObjectRef", ScriptFieldType::GameObject},
    TypeSpelling{"GameObject", ScriptFieldType::GameObject},
    TypeSpelling{"eng::AnimationRef", ScriptFieldType::Animation},
    TypeSpelling{"AnimationRef", ScriptFieldType::Animation},
    TypeSpelling{"eng::ImageRef", ScriptFieldType::Image},
    TypeSpelling{"ImageRef", ScriptFieldType::Image},
    TypeSpelling{"std::string", ScriptFieldType::String},
    TypeSpelling{"eng::Vec2", ScriptFieldType::Vec2},
    TypeSpelling{"Vector2", ScriptFieldType::Vec2},
    TypeSpelling{"Vec2", ScriptFieldType::Vec2},
    TypeSpelling{"eng::Color", ScriptFieldType::Color},
    TypeSpelling{"Color", ScriptFieldType::Color},
    TypeSpelling{"float", ScriptFieldType::Float},
    TypeSpelling{"bool", ScriptFieldType::Bool},
    TypeSpelling{"int", ScriptFieldType::Int},
};

// The words that may sit in front of a declaration, and whether each one rules
// the variable out of being tunable.
struct Qualifier {
    std::string_view word;
    bool             disqualifies;
};

constexpr std::array kQualifiers{
    Qualifier{"static", true},      // one value shared by every instance
    Qualifier{"constexpr", true},   // fixed when the script compiled
    Qualifier{"const", true},       // cannot be changed, so cannot be edited
    Qualifier{"mutable", false}, Qualifier{"inline", false},
};

// ---------------------------------------------------------------------------
//  A marker, and what is left of the comment once it is off the front
//
//  A bracket group AT THE VERY START is a marker. Only at the start, so that
//  ordinary prose mentioning "[1]" is left alone - a comment is documentation
//  first and a declaration second.
//
//  Returns whether one was there, and takes it off `text`.
// ---------------------------------------------------------------------------
bool TakeMarker(std::string& text, ScriptVarDecl& into) {
    if (!text.starts_with('[')) {
        return false;
    }
    const std::size_t close = text.find(']');
    if (close == std::string::npos) {
        return false;   // unfinished, so the whole thing stays as prose
    }

    const std::string marker = TrimmedCopy(text.substr(1, close - 1));
    text                     = TrimmedCopy(text.substr(close + 1));

    // "0..10" - a range, for a number.
    if (const std::size_t dots = marker.find(".."); dots != std::string::npos) {
        const std::string lowText  = TrimmedCopy(marker.substr(0, dots));
        const std::string highText = TrimmedCopy(marker.substr(dots + 2));

        // std::from_chars rather than std::stof: it does not throw, and a
        // marker somebody typed wrong should leave the variable alone rather
        // than take the whole build down.
        float low = 0.0f, high = 0.0f;
        const auto a = std::from_chars(lowText.data(),
                                       lowText.data() + lowText.size(), low);
        const auto b = std::from_chars(highText.data(),
                                       highText.data() + highText.size(), high);
        if (a.ec == std::errc{} && b.ec == std::errc{} && low < high) {
            into.ranged = true;
            into.min    = low;
            into.max    = high;
        }
    } else if (!marker.empty()) {
        // Anything else is a NAME, and which kind depends on the type it ends
        // up on: a component type or script for an EntityRef, a kind of asset
        // for a std::string. Neither is judged here - eng::ScriptFields::Ref
        // and ::Asset each report one they do not recognise, and the engine's
        // own lists are the only ones that should have an opinion.
        into.requiredType = marker;
    }
    return true;
}

// ---------------------------------------------------------------------------
//  THE COMMENT ON A DECLARATION, OR THE ONE DIRECTLY ABOVE IT
//
//  The trailing comment is read first and it wins. If it carries no marker,
//  the comment line IMMEDIATELY ABOVE the declaration is read for one.
//
//  ==========================================================================
//  WHY BOTH, when the trailing comment was the only place for a year.
//
//  A trailing marker only fits on the declaration's own line, and the
//  variables most in need of one are the variables with the most to explain.
//  An array of animation names with thirty lines of comment above it had to
//  carry `// [animation]` at the far right of the declaration, past the
//  tooltip, in the one place a reader of that comment block never looks.
//
//  THAT IS EXACTLY HOW THIS WAS REPORTED: a second array was added beside the
//  first, in the same shape, and came out as four plain text boxes - with the
//  difference being a comment nobody had seen, and the obvious conclusion
//  from outside being that the first array had been wired in by hand.
//
//  So a marker may now go where the variable is already being described:
//
//      // [animation] which clip to play walking left
//      std::string walkLeft;
//
//  ==========================================================================
//  IMMEDIATELY ABOVE, AND NOTHING FURTHER.
//
//  One line, with nothing between it and the declaration, and it has to START
//  with the bracket - the rule the trailing comment has always had. A banner
//  line, a blank line, or any line of code clears it.
//
//  Without that, a `[TODO]` eight lines up in a comment block would quietly
//  become a required type. A comment block is full of prose that a parser has
//  no business reading, and the further from the declaration a line is the
//  less likely it is to be about it.
// ---------------------------------------------------------------------------
void ReadComment(std::string_view trailing, std::string_view above,
                 ScriptVarDecl& into) {
    std::string rest = TrimmedCopy(trailing);

    if (!TakeMarker(rest, into)) {
        std::string aboveRest = TrimmedCopy(above);
        if (TakeMarker(aboveRest, into) && rest.empty()) {
            // The declaration had no comment of its own, so what followed the
            // marker above is the tooltip - there is nowhere else for one to
            // come from. A trailing comment, where there is one, says it
            // better and keeps the job.
            rest = aboveRest;
        }
    }

    into.tooltip = rest;
}

// ---------------------------------------------------------------------------
//  One declaration
//
//  Returns false when the line is not a variable declaration at all - a
//  member function, a nested type, a blank line. Those are not "skipped", they
//  were never candidates, and reporting them would bury the real reports.
// ---------------------------------------------------------------------------
bool ReadDeclaration(const SourceLine& line, std::string_view commentAbove,
                     const std::vector<std::string>& projectScripts,
                     ScriptVarDecl& outVar, ScriptVarSkipped& outSkipped,
                     bool& outWasSkipped) {
    outWasSkipped = false;

    std::string code = line.code;
    if (code.empty() || !code.ends_with(';')) {
        return false;   // a declaration is one line and ends in a semicolon
    }
    code.pop_back();

    // ======================================================================
    //  THE NAME IS NOT CONSULTED. PUBLIC MEANS SHOWN.
    //
    //  An `m_` prefix used to mean "not for the Inspector", silently - this
    //  project names its own internal members that way and the convention was
    //  read as a statement of intent.
    //
    //  IT WAS A TRAP, and it was reported as one. Half the C++ world names
    //  ORDINARY members m_something; somebody who does writes
    //  `m_walkSpeed` in a public section, gets no box, no skipped report and
    //  no explanation, and has no way to find out why. A rule that hides
    //  things by spelling is invisible precisely to the people it surprises.
    //
    //  The language already has a word for "not for the Inspector", and it is
    //  `private:`. That is what the docs now say, it is one line to write, and
    //  it cannot be arrived at by accident.
    //
    //  (The declarations that relied on this have moved into private sections,
    //  which is where they belonged - see assets/scripts/TilemapWalker.cpp.)
    // ======================================================================

    // ---- the words in front -------------------------------------------
    bool        disqualified = false;
    std::string disqualifier;
    bool        peeling = true;
    while (peeling) {
        peeling = false;
        for (const Qualifier& q : kQualifiers) {
            if (WordAt(code, 0, q.word)) {
                if (q.disqualifies) {
                    disqualified = true;
                    if (disqualifier.empty()) {
                        disqualifier = std::string(q.word);
                    }
                }
                code    = TrimmedCopy(code.substr(q.word.size()));
                peeling = true;
                break;
            }
        }
    }

    // ---- the type -----------------------------------------------------
    const TypeSpelling* matched = nullptr;
    for (const TypeSpelling& candidate : kTypeSpellings) {
        if (WordAt(code, 0, candidate.spelling)) {
            matched = &candidate;
            break;
        }
    }

    // ======================================================================
    //  THE LONG SPELLINGS: eng::ComponentRef<eng::SpriteComponent> for a
    //  component the engine defines, and eng::ScriptRef<Bullet> for a class
    //  the game defines.
    //
    //  A PREFIX, so the table above cannot hold it: every entry there is an
    //  exact word match. The component or the script is lifted out of the
    //  template argument textually and becomes the constraint, which is the
    //  one place in this file where a type name is read rather than
    //  recognised.
    //
    //  NO REGISTRY IS CONSULTED AND NONE COULD BE - this parser is pure, and
    //  the exposer is generated with no scene and no components registered.
    //  ScriptFields::AddRef already warns about a constraint naming nothing,
    //  in the one place that can actually answer the question.
    //
    //  THE TEXT IS ALREADY THE RIGHT STRING FOR BOTH, which is why one branch
    //  serves them: `eng::SpriteComponent` loses its `eng::` and equals
    //  SpriteComponent::kTypeName, and `Bullet` is already character-for-
    //  character what ENGINE_REGISTER_SCRIPT(Bullet) stringizes, what the
    //  registry keys on, and what ScriptComponent::ScriptName() hands
    //  EntityHasNamed. So a script constraint lands already matching.
    //
    //  The synthetic entry carries the WHOLE spelling, so the substr below
    //  steps past `ComponentRef<...>` without this branch having to know how.
    // ======================================================================
    // Named for the two of them, not for ComponentRef alone. NOTE that
    // `synthetic` below holds string_views INTO these, so anything factoring
    // this branch into a helper that returns a TypeSpelling returns dangling
    // views.
    std::string  refSpelling;
    std::string  refArgument;
    TypeSpelling synthetic{};
    if (matched == nullptr) {
        for (const std::string_view prefix :
             {std::string_view("eng::ComponentRef"), std::string_view("ComponentRef"),
              std::string_view("eng::ScriptRef"), std::string_view("ScriptRef")}) {
            if (!WordAt(code, 0, prefix)) {
                continue;
            }
            const std::size_t open = code.find('<', prefix.size());
            if (open == std::string::npos) {
                break;
            }

            // ---- THE MATCHING '>', COUNTED RATHER THAN GUESSED AT ---------
            //
            // rfind('>') reaches into the initialiser, and the result is the
            // one outcome this file works hardest to avoid: a variable that
            // vanishes with nothing reported. In
            //
            //     ScriptRef<Bullet> t = other.Get<Thing>();
            //
            // the last '>' is the one inside Get<Thing>, so the argument came
            // out as `Bullet> t = other.Get<Thing`, the synthetic spelling
            // swallowed the name, and the member was neither exposed nor
            // listed as skipped. Depth-counting from the opening '<' is the
            // whole fix, and it was wrong for ComponentRef the same way.
            std::size_t close = std::string::npos;
            for (std::size_t i = open, depth = 0; i < code.size(); ++i) {
                if (code[i] == '<') {
                    ++depth;
                } else if (code[i] == '>') {
                    if (--depth == 0) {
                        close = i;
                        break;
                    }
                }
            }
            if (close == std::string::npos) {
                break;
            }
            refArgument = TrimmedCopy(code.substr(open + 1, close - open - 1));
            if (refArgument.starts_with("eng::")) {
                refArgument.erase(0, 5);
            }
            if (refArgument.empty()) {
                break;
            }
            refSpelling = code.substr(0, close + 1);
            synthetic   = TypeSpelling{refSpelling, ScriptFieldType::Entity, refArgument};
            matched     = &synthetic;
            break;
        }
    }

    // ======================================================================
    //  A BARE SCRIPT NAME: `Door door;`
    //
    //  THE SPELLING THE WHOLE FEATURE EXISTS FOR, and the reason this
    //  function is given a list of names at all. `Door door;` and
    //  `Matrix4 transform;` are the same shape - two identifiers in a row -
    //  so nothing in the line itself can tell them apart. The only thing that
    //  can is whether some file in this project registers a script called
    //  Door, which is why the list is a parameter rather than a lookup.
    //
    //  THIS DOES NOT REINTERPRET THE DECLARATION; it reports the type the
    //  declaration genuinely has. In a generated unit the class lives in
    //  namespace s_Door and the bare name is an alias for
    //  eng::ScriptRef<s_Door::Door>, so `Door door;` IS a reference. See
    //  GenerateScriptUnit in tools/ScriptVars.h for why it is arranged that
    //  way and what it costs.
    //
    //  WHOLE-WORD, which is what keeps `Door* door;` out of here: the star
    //  stops the match, so that spelling still reaches the pointer branch
    //  below - which now knows to recommend `Door door;` rather than a
    //  wrapper the author would otherwise have to be taught.
    // ======================================================================
    if (matched == nullptr && !projectScripts.empty()) {
        std::size_t end = 0;
        while (end < code.size() && IsIdentChar(code[end])) {
            ++end;
        }
        // WHITESPACE HAS TO FOLLOW IT, which is what keeps `Door* door;` out.
        // Matching the name alone would take the star with it as a reference
        // to a supported type, and that path's message is the short "it is a
        // pointer" - not the one that tells the author to drop the star and
        // write the name on its own. The teaching message is the whole value
        // of catching this spelling, so the match has to miss it.
        if (end > 0 && end < code.size() && (code[end] == ' ' || code[end] == '\t')) {
            const std::string leading = code.substr(0, end);
            for (const std::string& script : projectScripts) {
                if (leading == script) {
                    refSpelling = leading;
                    refArgument = leading;
                    synthetic =
                        TypeSpelling{refSpelling, ScriptFieldType::Entity, refArgument};
                    matched = &synthetic;
                    break;
                }
            }
        }
    }

    std::string afterType;
    if (matched != nullptr) {
        afterType = TrimmedCopy(code.substr(matched->spelling.size()));
    } else {
        // NOT A TYPE THIS CAN DRAW - but is it a variable at all? Reporting
        // `double speed = 1.0` as unsupported is worth doing; reporting every
        // line in the file is not. The test is two identifiers in a row, which
        // is what a declaration looks like and an expression does not.
        const std::size_t space = code.find_first_of(" \t");
        if (space == std::string::npos) {
            return false;
        }
        const std::string typeText = code.substr(0, space);
        const std::string nameText = TrimmedCopy(code.substr(space));

        // ================================================================
        //  IS IT A POINTER? ASKED HERE, AND THAT PLACEMENT IS THE FIX.
        //
        //  The branch further down that says "it is a pointer, and the
        //  Inspector edits values" reads `afterType`, which only exists once
        //  the type has MATCHED one of kTypeSpellings. For a type this file
        //  has never heard of - every component, and every class a project
        //  writes - it was therefore unreachable, and a perfectly ordinary
        //  `SpriteComponent* m_sprite` was told that its TYPE is not one the
        //  Inspector can draw, followed by a list of scalars to pick from
        //  instead. The answer was never the type; it is that an address has
        //  nothing to put in a box.
        //
        //  `SpriteComponent *m_sprite` was worse still: the star lands on the
        //  front of nameText, failed the IsIdentChar test that used to be
        //  here, and the member was reported as NOTHING AT ALL. Three
        //  spellings of one intent gave two near-identical messages and a
        //  silence, and silence is the outcome this file works hardest to
        //  avoid - see the note at the top of ScriptVars.h.
        //
        //  THE STAR BELONGS TO WHICHEVER SIDE IT WAS WRITTEN ON, so both are
        //  checked and neither spelling is the special case.
        //
        //  AND A POINTER NEED NOT HAVE A STAR AT ALL. Engine.h still hands
        //  scripts two aliases that ARE pointers - Collider2D and GameObject
        //  - so `Collider2D area = nullptr;` is a pointer with no star in
        //  sight. Initialising from nullptr is the one thing a value cannot
        //  do, which tells the two apart without this parser having to know
        //  what any of those names mean. It stays pure: no registry, no
        //  engine state. See ScriptVars.h.
        //
        //  THIS LIST USED TO NAME Rigidbody2D AND SpriteRenderer TOO, and it
        //  had gone stale: both are eng::ComponentRef now, so the
        //  `Rigidbody2D m_body = nullptr;` this comment called correct does
        //  not compile at all, and TilemapWalker.cpp - cited here as the
        //  worked example - writes `eng::Rigidbody2DComponent* m_body =
        //  nullptr;` instead. A `= nullptr` beside a ComponentRef still lands
        //  in this branch and is still told it is a pointer, which is the
        //  best this parser can do: the line is wrong whichever it is.
        //
        //  AND IT NO LONGER NAMES GameObject EITHER, which is a sharper
        //  version of the same staleness: that one is a value now - an
        //  eng::GameObjectRef - and it ACCEPTS nullptr on purpose, so that
        //  every line written against the old pointer typedef goes on
        //  compiling. "Initialising from nullptr is the one thing a value
        //  cannot do" is therefore no longer true of it, and the premise this
        //  whole test rests on has one exception.
        //
        //  `GameObject target = nullptr;` is the spelling somebody will write
        //  out of habit, and it is correct. Being told it is a pointer and
        //  watching the field vanish is the worst outcome available, so the
        //  exception is spelled out rather than left to the heuristic.
        //  STILL PURE: it is a comparison against a list in this file, which
        //  is what kTypeSpellings already is - no registry, no engine state.
        // ================================================================
        const bool valueTakesNullptr = WordAt(typeText, 0, "GameObject") ||
                                       WordAt(typeText, 0, "GameObjectRef") ||
                                       WordAt(typeText, 0, "eng::GameObjectRef");

        const bool starOnType =
            !typeText.empty() && (typeText.back() == '*' || typeText.back() == '&');
        const bool starOnName =
            !nameText.empty() && (nameText.front() == '*' || nameText.front() == '&');
        const std::size_t assign = nameText.find('=');
        const bool fromNullptr =
            !valueTakesNullptr && assign != std::string::npos &&
            TrimmedCopy(nameText.substr(assign + 1)).starts_with("nullptr");

        if (starOnType || starOnName || fromNullptr) {
            const char mark = starOnType ? typeText.back()
                            : starOnName ? nameText.front()
                                         : '*';
            const std::string rest =
                starOnName ? TrimmedCopy(nameText.substr(1)) : nameText;
            const std::size_t end = rest.find_first_not_of(
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_");
            outSkipped.name   = end == std::string::npos ? rest : rest.substr(0, end);
            outSkipped.reason =
                std::string("it is a ") + (mark == '*' ? "pointer" : "reference") +
                ", and the Inspector edits values. For ANOTHER SCRIPT on another "
                "entity, drop the star and write the script's own name - "
                "`TheScript member;` is already a reference you can drag an entity "
                "onto, and `member->Method()` calls it. For a component the engine "
                "defines, the type to write is TransformRef, SpriteRenderer, "
                "Rigidbody2D or eng::ComponentRef<TheComponent> - see "
                "scene/ComponentRef.h and scene/ScriptRef.h. For a plain "
                "`Entity*`, the type is eng::EntityRef - the Inspector draws it "
                "and it survives a save, which a pointer cannot. For one on THIS "
                "entity, which is what a pointer beside a script usually is, ask "
                "with Find<eng::TheComponent>() where you use it - and for a "
                "SCRIPT on this entity, with "
                "eng::FindScriptOn<TheScript::ScriptType>(*Owner()). THE "
                "::ScriptType IS NOT OPTIONAL: a bare script name means a "
                "REFERENCE to the script, so FindScriptOn<TheScript> asks for "
                "something no entity carries - it used to compile and return "
                "null for ever, and is now refused with a static_assert saying "
                "this";
            outWasSkipped = !outSkipped.name.empty();
            return false;
        }

        if (typeText.empty() || nameText.empty() || !IsIdentChar(typeText.front()) ||
            !IsIdentChar(nameText.front())) {
            return false;
        }
        // An assignment or a bare name - and nothing that looks like a call.
        const std::size_t equals = nameText.find('=');
        const std::string declarator =
            equals == std::string::npos ? nameText : TrimmedCopy(nameText.substr(0, equals));
        if (declarator.empty() ||
            declarator.find_first_of("()<>*&,[]") != std::string::npos ||
            declarator.find_first_of(" \t") != std::string::npos) {
            return false;
        }

        // ImageRef AND AnimationRef WERE MISSING FROM THIS LIST, which the
        // table at the top of this file has handled all along and the
        // Inspector draws. The omission was visible in one build's output:
        // Bullet.cpp declares `ImageRef sprite;`, the very next line read
        // "exposing sprite, ...", and this sentence denied the type worked.
        //
        // THE SECOND SENTENCE IS CONDITIONAL ON PURPOSE. This branch sees any
        // type it does not know, which includes a project's own plain structs,
        // so it must not assert that the member IS a component - only say what
        // follows if it is. A component held by value is never right: the
        // entity owns it, and a script asks for it where it is used.
        // THE SUGGESTION MUST NOT DOUBLE THE NAMESPACE. Pasting the author's
        // own token after a literal "Find<eng::" produced
        // Find<eng::eng::SpriteAnimationComponent>() for anybody who had
        // qualified the member's type - an instruction that cannot compile,
        // handed out at the moment they were already stuck.
        const std::string qualified =
            typeText.starts_with("eng::") ? typeText : "eng::" + typeText;

        // AND THE LIST OMITTED EVERY REFERENCE SPELLING THE INSPECTOR DRAWS,
        // which made this the most misleading message in the file: a reader
        // who had just declared a component by value concluded that no
        // component could be drawn at all, when in fact ComponentRef, the
        // Engine.h aliases and a bare script name are all drawn as slots.
        // The authority is fields::Traits<T>::known, not this sentence; see
        // scene/ScriptFields.h.
        outSkipped.name   = declarator;
        outSkipped.reason = "its type '" + typeText +
                            "' is not one the Inspector can draw - it handles float, "
                            "int, bool, std::string, Vec2, Color, EntityRef, "
                            "PrefabRef, ImageRef, AnimationRef, a REFERENCE to a "
                            "component (eng::ComponentRef<T>, or one of Engine.h's "
                            "aliases - TransformRef, SpriteRenderer, Rigidbody2D and "
                            "the rest) and a REFERENCE to another script, which is "
                            "simply its own name. If '" + typeText +
                            "' is a component, this declares one by value, which is "
                            "not how a script holds one. Two ways round it: ask for "
                            "it where you use it with Find<" + qualified +
                            ">(), which hands back a pointer, or declare "
                            "eng::ComponentRef<" + qualified +
                            "> and drag an entity onto the slot";
        outWasSkipped = true;
        return false;
    }

    // A pointer or a reference to one of the supported types. Not exposable:
    // the Inspector edits a value, and there is nothing sensible to put in a
    // box for an address.
    if (afterType.starts_with('*') || afterType.starts_with('&')) {
        const std::string rest = TrimmedCopy(afterType.substr(1));
        const std::size_t end  = rest.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_");
        outSkipped.name = end == std::string::npos ? rest : rest.substr(0, end);
        outSkipped.reason =
            std::string("it is a ") + (afterType.front() == '*' ? "pointer" : "reference") +
            ", and the Inspector edits values";
        outWasSkipped = !outSkipped.name.empty();
        return false;
    }

    // ---- the name -----------------------------------------------------
    std::size_t nameEnd = 0;
    while (nameEnd < afterType.size() && IsIdentChar(afterType[nameEnd])) {
        ++nameEnd;
    }
    if (nameEnd == 0) {
        return false;   // the type was the start of something else entirely
    }
    const std::string name = afterType.substr(0, nameEnd);
    std::string       tail = TrimmedCopy(afterType.substr(nameEnd));

    // ==================================================================
    //  A FIXED-SIZE ARRAY: "std::string walkAnimations[4];"
    //
    //  The brackets are part of the DECLARATOR, not of the type and not of
    //  the initialiser, so they come off here and everything below judges the
    //  rest of the line exactly as it would for a plain member. That is the
    //  whole of the parsing: the LENGTH IS NEVER READ, because the generated
    //  exposer loops over std::size() and so the declaration may say [4] or
    //  [kFacingCount] or anything else the compiler can count. Parsing it
    //  here would be a second opinion about a number that is already known.
    //
    //  "a[2][2]" is left alone rather than half-understood - one group, at
    //  the end, or it is not something this handles.
    //
    //  See ScriptVarDecl::isArray for why one row per element is the whole
    //  feature and a list widget is not needed.
    // ==================================================================
    bool isArray = false;
    if (tail.starts_with('[')) {
        const std::size_t close = tail.find(']');
        if (close == std::string::npos) {
            return false;   // unfinished; there is nothing useful to say about it
        }
        if (tail.find('[', close + 1) != std::string::npos) {
            return false;   // two dimensions
        }
        isArray = true;
        tail    = TrimmedCopy(tail.substr(close + 1));
    }

    // WHAT FOLLOWS THE NAME decides what this line is.
    //
    //   "=" or "{"  an initialised member          -> a variable
    //   ""          a member with no initialiser   -> a variable
    //   "("         a function                     -> not a candidate at all
    //   ","         several declarators at once    -> reported, not guessed at
    if (tail.starts_with('(')) {
        return false;
    }
    if (!tail.empty() && !tail.starts_with('=') && !tail.starts_with('{') &&
        !tail.starts_with(',')) {
        return false;
    }

    // ==================================================================
    //  SEVERAL DECLARATORS ON ONE LINE, which has to be found AFTER the
    //  initialiser rather than right after the name.
    //
    //  Checking only the character following the name catches `float a, b;`
    //  and misses `float a = 1.0f, b = 2.0f;` - where the comma is four
    //  tokens later. A test caught that, and the second form is the more
    //  likely one to be written.
    //
    //  The comma has to be at the TOP LEVEL. `Vec2 offset{3.0f, -4.0f}` and
    //  `Color tint{10, 20, 30, 40}` are single declarators whose initialisers
    //  are full of commas, and refusing those would be worse than useless.
    // ==================================================================
    int nesting = 0;
    for (const char c : tail) {
        if (c == '(' || c == '{' || c == '[') {
            ++nesting;
        } else if (c == ')' || c == '}' || c == ']') {
            --nesting;
        } else if (c == ',' && nesting <= 0) {
            outSkipped.name   = name;
            outSkipped.reason = "it shares its line with another variable - give it a "
                                "line of its own to have it shown";
            outWasSkipped = true;
            return false;
        }
    }

    if (disqualified) {
        outSkipped.name = name;
        outSkipped.reason =
            "it is " + disqualifier +
            ", so it cannot be changed - write it as an ordinary member to tune it";
        outWasSkipped = true;
        return false;
    }

    outVar           = ScriptVarDecl{};
    outVar.name      = name;
    outVar.type      = std::string(matched->spelling);
    outVar.fieldType = matched->type;
    outVar.isArray   = isArray;
    ReadComment(line.comment, commentAbove, outVar);

    // A range only means something on a number, and a required type only on a
    // reference. Dropped rather than carried, so nothing downstream has to ask
    // whether it applies.
    if (outVar.fieldType != ScriptFieldType::Float &&
        outVar.fieldType != ScriptFieldType::Int) {
        outVar.ranged = false;
    }
    // ======================================================================
    //  A MARKER MEANS SOMETHING ON A STRING NOW, as well as on a reference.
    //
    //  On an EntityRef, `[Rigidbody2DComponent]` narrows the picker. On a
    //  std::string, `[image]` or `[animation]` says the text is an ASSET PATH
    //  - which turns the box into the same slot a Sprite's texture gets: the
    //  file's name, a drop target and a picker, instead of a path somebody has
    //  to type correctly from memory.
    //
    //  It stays opt-in because a string is usually just a string. A label's
    //  text is not a file, and guessing would put a file picker on it.
    // ======================================================================
    if (outVar.fieldType != ScriptFieldType::Entity &&
        outVar.fieldType != ScriptFieldType::String) {
        outVar.requiredType.clear();
    }

    // ---- a component reference constrains itself ------------------------
    //
    // AFTER the marker, so a hand-written `// [BoxColliderComponent]` still
    // wins: somebody who wrote one meant it, and narrowing a Collider2D slot
    // to one kind of collider is a reasonable thing to want. With no marker
    // the type is the constraint, which is the whole argument for the type.
    if (!matched->required.empty() && outVar.requiredType.empty()) {
        outVar.requiredType = std::string(matched->required);
    }
    return true;
}

// Turns text into a C++ string literal's contents.
std::string Escaped(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (const char c : text) {
        if (c == '\\' || c == '"') {
            out += '\\';
            out += c;
        } else if (c != '\r' && c != '\n') {
            out += c;
        }
    }
    return out;
}

// A number as C++ source, in the SAME TYPE as the variable it bounds.
//
// This is load-bearing. ScriptFields::Range takes (T&, T, T), so handing an
// int variable a 0.0f bound makes the deduction ambiguous, the `requires`
// guard false, and the slider silently absent. The literal has to match.
std::string BoundLiteral(ScriptFieldType type, float value) {
    char text[64];
    if (type == ScriptFieldType::Int) {
        std::snprintf(text, sizeof(text), "%d", static_cast<int>(value));
    } else {
        std::snprintf(text, sizeof(text), "%.6ff", static_cast<double>(value));
    }
    return text;
}

// ---------------------------------------------------------------------------
//  THE FIVE NAMES A SCRIPT DOES NOT OWN.
//
//  eng::ScriptBehaviour has public handle members called transform,
//  gameObject, scene, name and tag - see the long note at the top of
//  scene/ScriptComponent.h. Two different mistakes both arrive here as a
//  declaration of one of those names, and neither can be exposed:
//
//    A MEMBER OF THE USER'S OWN by that name HIDES the inherited handle. The
//    Inspector would show a row labelled "tag" with nothing to do with the
//    entity's tag, while HasTag() and FindEntityWithTag went on reading the
//    real one - two answers that disagree for ever, with nothing said.
//
//    A VARIABLE THIS PARSER ONLY THOUGHT IT SAW - inside an `#if 0`, behind a
//    macro, or past a miscounted raw string - used to be discarded by the
//    `requires` guard in GenerateExposerUnit, because no member of that name
//    existed. Against these five it RESOLVES, to the handle.
//
//  Either way the answer is to leave it out and say why in the Console, which
//  is what the skipped report is for.
// ---------------------------------------------------------------------------
std::string_view InheritedMemberReason(std::string_view name) {
    if (name == "transform" || name == "gameObject" || name == "scene" ||
        name == "name" || name == "tag") {
        return "eng::ScriptBehaviour already has a member of that name - one of "
               "the bare names every script inherits. A member of your own by "
               "that name hides it, so give yours a different one";
    }
    return {};
}

} // namespace

// ---------------------------------------------------------------------------
//  Scanning one source file
// ---------------------------------------------------------------------------

// The scan with nothing known about the project: every caller that has no
// project behind it - the tests, a lone file, anything asking only "does this
// register a script?" - and identical to the one below for everything except a
// bare script name, which it cannot recognise because nothing told it which
// names are scripts.
ScriptSourceInfo ScanScriptSource(std::string_view source) {
    return ScanScriptSource(source, std::vector<std::string>{});
}

ScriptSourceInfo ScanScriptSource(std::string_view                source,
                                  const std::vector<std::string>& projectScripts) {
    ScriptSourceInfo info;

    const std::vector<SourceLine> lines = SplitSource(source);

    // ---- which class does this file register? ----------------------------
    //
    // From ENGINE_REGISTER_SCRIPT, because that is the only place a script's
    // name is written down - and it is in CODE, so the banner comment at the
    // top of every generated script that mentions the macro cannot be
    // mistaken for it.
    std::size_t registerLine = std::string::npos;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::size_t at = FindWord(lines[i].code, "ENGINE_REGISTER_SCRIPT");
        if (at == std::string::npos) {
            continue;
        }
        const std::size_t open = lines[i].code.find('(', at);
        if (open == std::string::npos) {
            continue;
        }
        std::size_t cursor = lines[i].code.find_first_not_of(" \t", open + 1);
        std::string name;
        while (cursor != std::string::npos && cursor < lines[i].code.size() &&
               IsIdentChar(lines[i].code[cursor])) {
            name += lines[i].code[cursor++];
        }
        if (!name.empty()) {
            info.className = name;
            registerLine   = i;
            break;
        }
    }

    if (info.className.empty()) {
        return info;   // not a script: a helper, or a file still being written
    }

    // ---- where is that class defined? ------------------------------------
    //
    // Searched for as `class Name` or `struct Name`, and a FORWARD DECLARATION
    // is stepped over: `class Orbiter;` names the class without containing it,
    // and stopping there would find no variables and report none.
    std::size_t bodyLine = std::string::npos;
    std::size_t bodyCol  = std::string::npos;
    bool        isStruct = false;

    for (std::size_t i = 0; i < lines.size() && bodyLine == std::string::npos; ++i) {
        for (const std::string_view keyword : {"class", "struct"}) {
            std::size_t at = FindWord(lines[i].code, keyword);
            while (at != std::string::npos) {
                const std::size_t nameAt =
                    lines[i].code.find_first_not_of(" \t", at + keyword.size());
                if (nameAt != std::string::npos &&
                    WordAt(lines[i].code, nameAt, info.className)) {
                    // Scan forward for the '{' that opens the body, across as
                    // many lines as the base clause takes. A ';' first means
                    // this was only a declaration.
                    std::size_t scanLine = i;
                    std::size_t scanCol  = nameAt + info.className.size();
                    bool        declared = false;
                    while (scanLine < lines.size() && !declared) {
                        const std::string& text = lines[scanLine].code;
                        while (scanCol < text.size()) {
                            if (text[scanCol] == '{') {
                                bodyLine = scanLine;
                                bodyCol  = scanCol;
                                isStruct = keyword == "struct";
                                break;
                            }
                            if (text[scanCol] == ';') {
                                declared = true;
                                break;
                            }
                            ++scanCol;
                        }
                        if (bodyLine != std::string::npos || declared) {
                            break;
                        }
                        ++scanLine;
                        scanCol = 0;
                    }
                }
                if (bodyLine != std::string::npos) {
                    break;
                }
                at = FindWord(lines[i].code, keyword, at + 1);
            }
            if (bodyLine != std::string::npos) {
                break;
            }
        }
    }

    if (bodyLine == std::string::npos) {
        // The file registers a class this cannot find - a script written with
        // a macro, or in a way the whitelist does not cover. Nothing is
        // exposed automatically and OnExpose still works, which is exactly
        // the behaviour before any of this existed.
        return info;
    }

    // ---- walk the body ---------------------------------------------------
    //
    // `class` starts private and `struct` starts public, which is the one
    // place the two keywords differ here - and getting it backwards would
    // expose a class's private members or hide a struct's public ones.
    bool isPublic = isStruct;
    int  depth    = 0;

    for (std::size_t i = bodyLine; i < lines.size(); ++i) {
        const SourceLine& line = lines[i];
        const std::size_t from = (i == bodyLine) ? bodyCol : 0;

        // DEPTH IS COUNTED FIRST, over the whole line, so that a declaration
        // sharing its line with a brace cannot be read at the wrong depth.
        int depthBefore = depth;
        for (std::size_t c = from; c < line.code.size(); ++c) {
            if (line.code[c] == '{') {
                ++depth;
            } else if (line.code[c] == '}') {
                --depth;
            }
        }

        if (i == bodyLine) {
            // The opening line. Anything after the '{' on it is body text, but
            // this is rare enough not to chase.
            if (FindWord(line.code, "OnExpose") != std::string::npos) {
                info.hasOnExpose = true;
            }
            continue;
        }

        if (FindWord(line.code, "OnExpose") != std::string::npos) {
            info.hasOnExpose = true;
        }

        // Only the class's own members, not anything nested inside a function
        // or a nested type.
        if (depthBefore == 1) {
            std::string code = line.code;

            // An access specifier, which may be followed by a declaration on
            // the same line.
            for (const std::string_view word : {"public", "private", "protected"}) {
                if (WordAt(code, 0, word)) {
                    const std::size_t colon = code.find(':', word.size());
                    if (colon != std::string::npos) {
                        isPublic = (word == "public");
                        code     = TrimmedCopy(code.substr(colon + 1));
                    }
                    break;
                }
            }

            if (isPublic && !code.empty()) {
                // ==========================================================
                //  THE COMMENT LINE DIRECTLY ABOVE, which is where the marker
                //  is when the declaration's own comment has none.
                //
                //  A line that is NOTHING BUT a comment, with nothing between
                //  it and this one. A blank line carries no comment and a line
                //  of code is not one, so either clears it - which is what
                //  makes "immediately above" mean that. See ReadComment.
                // ==========================================================
                const std::string_view commentAbove =
                    (i > bodyLine && lines[i - 1].code.empty())
                        ? std::string_view(lines[i - 1].comment)
                        : std::string_view{};

                SourceLine candidate{code, line.comment};
                ScriptVarDecl    found;
                ScriptVarSkipped skipped;
                bool             wasSkipped = false;

                if (ReadDeclaration(candidate, commentAbove, projectScripts, found,
                                    skipped, wasSkipped)) {
                    // One of ScriptBehaviour's own names is not this script's
                    // to expose - see InheritedMemberReason above for the two
                    // quite different ways a declaration gets here.
                    if (const std::string_view why = InheritedMemberReason(found.name);
                        !why.empty()) {
                        info.skipped.push_back({found.name, std::string(why)});
                    } else {
                        info.vars.push_back(std::move(found));
                    }
                } else if (wasSkipped) {
                    info.skipped.push_back(std::move(skipped));
                }
            }
        }

        if (depth <= 0) {
            break;   // the class body has closed
        }
    }

    (void)registerLine;
    return info;
}

// ---------------------------------------------------------------------------
//  Generating the translation unit
// ---------------------------------------------------------------------------

// ===========================================================================
//  THE SECOND HALF OF EVERY GENERATED UNIT: the exposer registration, and one
//  guarded lambda per variable.
//
//  SHARED BY BOTH UNIT WRITERS, which differ only in how the script's text
//  reached the translation unit above this point - an #include at global
//  scope, or a copy inside a namespace of its own.
//
//  `castTarget` is how THIS unit spells the class: the bare name when the
//  source was included at global scope, and s_Name::Name when it was wrapped
//  so that the bare name could become the reference type. Everything below
//  this line is identical either way, which is the point of the split.
// ===========================================================================
std::string GenerateExposerFor(const ScriptSourceInfo& info,
                               std::string_view        castTarget) {
    std::string out;
    out.reserve(512 + info.vars.size() * 256);

    out += "namespace {\n";
    out += "struct EngineAutoExpose_";
    out += info.className;
    out += " {\n    EngineAutoExpose_";
    out += info.className;
    out += "() {\n";
    out += "        ::eng::ScriptRegistry::SetExposer(\"";
    out += info.className;
    out += "\",\n";
    out += "            [](::eng::ScriptBehaviour* behaviour, ::eng::ScriptFields& "
           "fields) {\n";
    out += "                auto* self = static_cast<";
    out += castTarget;
    out += "*>(behaviour);\n";
    out += "                (void)self; (void)fields;\n";

    for (const ScriptVarDecl& var : info.vars) {
        // ================================================================
        //  EVERY VARIABLE IS WRAPPED IN A GENERIC LAMBDA, and that is what
        //  makes generated code unable to break the build.
        //
        //  `if constexpr (requires { ... })` only turns a failure into false
        //  when the failure happens while substituting a TEMPLATE PARAMETER -
        //  the same rule ScriptHooks.h documents for its hook detection.
        //  Written against the concrete class it would be a hard error, so
        //  the lambda takes `S` and the class is substituted into it.
        //
        //  The effect: a variable this parser imagined, or one that turned out
        //  to be inaccessible, COMPILES AWAY instead of failing rather than
        //  taking somebody's build down from a generated file.
        //
        //  IT IS THEN ABSENT FROM THE INSPECTOR AND FROM THE SKIPPED REPORT
        //  BOTH, and this comment used to claim the second one caught it. It
        //  cannot: info.skipped is filled in while READING the source, and a
        //  variable that reaches this loop was accepted - it is in info.vars,
        //  the Console has already named it in "exposing ...", and the guard
        //  is the compiler disagreeing afterwards. A variable could be
        //  announced and then vanish with nothing said.
        //
        //  The check emitted after this loop is what makes the claim true,
        //  from the one place that can see the answer. See the note on it.
        //
        //  AND THE GUARD HAS TO ASK ABOUT THE TYPE SEPARATELY, which is the
        //  part that is easy to get wrong. ScriptFields::Var is an
        //  unconstrained template whose static_assert is in its BODY, and a
        //  requires-expression never instantiates a body - so
        //  `requires { f.Var(n, v); }` is SATISFIED for a type the Inspector
        //  cannot draw, the branch goes live, and that assert then fails the
        //  user's build from a generated file. Which is the one thing this
        //  file promises cannot happen.
        //
        //  THE TYPE CHECK IS A NESTED REQUIREMENT, inside the
        //  requires-expression rather than beside it with `&&`. Beside it,
        //  `decltype(s->x)` would no longer be in a SFINAE context, so a
        //  variable this parser imagined that resolves to NOTHING - much the
        //  commoner case - would become a hard "is not a member of" error
        //  instead of compiling away. Nested, both kinds are simply false.
        // ================================================================
        // ================================================================
        //  WHICH CALL, and the String case is the one that bites.
        //
        //  A marker now survives on a std::string as well as on an EntityRef,
        //  so "has a requiredType" no longer means "is a reference". Choosing
        //  Ref for a string compiled - the `requires` guard below swallowed it
        //  - and the variable then silently did not appear, which is the whole
        //  class of bug that guard exists to avoid being loud about.
        // ================================================================
        const std::string call =
            var.ranged ? "Range"
            : var.requiredType.empty()
                ? "Var"
                : (var.fieldType == ScriptFieldType::String ? "Asset" : "Ref");

        // ================================================================
        //  AN ARRAY BECOMES ONE CALL PER ELEMENT, inside a loop.
        //
        //  The name is built at run time - "walkAnimations[0]" - which is
        //  safe because ScriptFields::Add COPIES the name rather than keeping
        //  a view into it. Every other part of the system then sees four
        //  ordinary variables with four distinct names, and needs to know
        //  nothing about arrays: the widgets, the scene file, the prefab
        //  overrides and the undo stack all work as they already did.
        //
        //  std::size() rather than a parsed length, so the declaration may
        //  say [4] or [kFacingCount] or anything else that counts.
        // ================================================================
        const std::string element = var.isArray ? "s->" + var.name + "[i]"
                                                : "s->" + var.name;
        const std::string nameArg =
            var.isArray ? "::std::string(\"" + Escaped(var.name) +
                              "[\") + ::std::to_string(i) + \"]\""
                        : "\"" + Escaped(var.name) + "\"";

        std::string args = nameArg + ", " + element;
        if (var.ranged) {
            args += ", " + BoundLiteral(var.fieldType, var.min);
            args += ", " + BoundLiteral(var.fieldType, var.max);
        } else if (!var.requiredType.empty()) {
            args += ", \"" + Escaped(var.requiredType) + "\"";
        }

        out += "                []<class S>(S* s, ::eng::ScriptFields& f) {\n";
        if (var.isArray) {
            out += "                    for (::std::size_t i = 0; i < ::std::size(s->" +
                   var.name + "); ++i) {\n";
        }
        out += "                    if constexpr (requires { f." + call + "(" + args +
               "); requires ::eng::fields::Traits< ::std::remove_cvref_t<decltype(" +
               element + ")> >::known; }) {\n";
        out += "                        f." + call + "(" + args;
        if (!var.tooltip.empty()) {
            out += ", \"" + Escaped(var.tooltip) + "\"";
        }
        out += ");\n";
        out += "                    }\n";
        if (var.isArray) {
            out += "                    }\n";
        }
        out += "                }(self, fields);\n";
    }

    // =======================================================================
    //  AND THEN: DID EVERY VARIABLE ACTUALLY ARRIVE?
    //
    //  THE GAP THIS CLOSES was in the comment above rather than in the code,
    //  which is the worst place for one to be. It said a variable whose guard
    //  turns out false is "absent from the Inspector and present in the
    //  skipped report". The first half is true. The second never was: the
    //  skipped report is info.skipped, which this parser fills in while
    //  READING the source, and a variable that got this far is in info.vars -
    //  it was accepted, the Console has already announced it by name, and the
    //  guard below is the compiler disagreeing hours later. It then appeared
    //  in neither list and nothing said a word.
    //
    //  SO THE GENERATED CODE ASKS, because it is the only thing present at
    //  the moment the answer exists. The parser cannot know - the guard is
    //  resolved by the compiler, against the real class, long after the scan.
    //  The editor cannot know - it would have to construct a script to find
    //  out. The exposer runs with a real object and a real field list in
    //  front of it.
    //
    //  ONCE PER LIBRARY LOAD, not once per component: the static lives in the
    //  script's own DLL, so it resets when that library is rebuilt and
    //  reloaded - which is exactly when the answer could have changed.
    //
    //  AN ARRAY IS ASKED ABOUT BY ITS FIRST ELEMENT. The loop that expands it
    //  runs over std::size() at run time, so there is no static list of names
    //  to check - but the guard is inside that loop and does not vary with
    //  the index, so either every element arrived or none did.
    // =======================================================================
    if (!info.vars.empty()) {
        out += "\n                static bool engineExposeChecked = false;\n";
        out += "                if (!engineExposeChecked) {\n";
        out += "                    engineExposeChecked = true;\n";
        out += "                    for (const char* expected : {";
        bool first = true;
        for (const ScriptVarDecl& var : info.vars) {
            if (!first) {
                out += ", ";
            }
            first = false;
            out += '"';
            out += Escaped(var.name);
            out += var.isArray ? "[0]\"" : "\"";
        }
        out += "}) {\n";
        out += "                        if (fields.Find(expected) != nullptr) {\n";
        out += "                            continue;\n";
        out += "                        }\n";
        out += "                        ENGINE_LOG_WARN(::eng::Channels::kScene,\n";
        out += "                            \"the script '";
        out += Escaped(info.className);
        out += "' declares '{}', and the editor read it as a variable - but the "
               "compiler could not expose it, so it is not in the Inspector. Its "
               "type is not one the Inspector can draw, or it is not reachable "
               "from outside the class. Give it a type from the list in "
               "scene/ScriptFields.h, or make it private to say it is not "
               "meant to be tuned.\", expected);\n";
        out += "                    }\n";
        out += "                }\n";
    }

    out += "            });\n    }\n};\n";
    out += "const EngineAutoExpose_";
    out += info.className;
    out += " g_engineAutoExpose_";
    out += info.className;
    out += ";\n} // namespace\n";
    return out;
}

std::string GenerateExposerUnit(const ScriptSourceInfo& info,
                                std::string_view        includePath) {
    std::string out;
    out.reserve(1024 + info.vars.size() * 256);

    out += "// Written by the editor. Do not edit, and do not commit: it is\n";
    out += "// regenerated from the script's own source every time it builds.\n";
    out += "//\n";
    out += "// It INCLUDES the script rather than copying it, so every line number,\n";
    out += "// every __FILE__ and the file's own static helpers stay exactly as they\n";
    out += "// were - a compiler error still points at the line you wrote.\n";
    out += "//\n";
    out += "// Three of the standard headers are for the loop an ARRAY variable\n";
    out += "// expands to - std::size, std::to_string and std::size_t - and\n";
    out += "// <type_traits> is for the std::remove_cvref_t in every guard below.\n";
    out += "// Named rather than relied on through the engine's own headers, because\n";
    out += "// a generated file that compiles only because of what something else\n";
    out += "// happened to include is a file that breaks for a reason nobody can see.\n";
    out += "#include <cstddef>\n";
    out += "#include <iterator>\n";
    out += "#include <string>\n";
    out += "#include <type_traits>\n\n";
    out += "#include \"";
    out += includePath;
    out += "\"\n\n";

    out += GenerateExposerFor(info, info.className);
    return out;
}

// ---------------------------------------------------------------------------
//  Pulling a script's #include lines out of its body.
//
//  ASKED OF THE COMMENT-STRIPPED CODE, not of the raw line, so a `#include`
//  written inside a block comment - which a file explaining how includes work
//  is quite likely to contain - is not hoisted into a real one. SplitSource is
//  one entry per line, so the two run in step.
//
//  EACH HOISTED LINE LEAVES A BLANK BEHIND. The body has to keep its exact
//  line count or the #line directive above it stops being true and every
//  diagnostic in the file points one line too high.
// ---------------------------------------------------------------------------
namespace {

bool LineIsAnInclude(const std::string& code) {
    std::size_t at = code.find_first_not_of(" \t");
    if (at == std::string::npos || code[at] != '#') {
        return false;
    }
    at = code.find_first_not_of(" \t", at + 1);
    return at != std::string::npos && code.compare(at, 7, "include") == 0;
}

void SplitOutIncludes(const std::string&        text,
                      std::vector<std::string>& outIncludes,
                      std::string&              outBody) {
    const std::vector<SourceLine> scanned = SplitSource(text);

    std::size_t start = 0;
    std::size_t index = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::string raw = text.substr(start, end - start);
        if (!raw.empty() && raw.back() == '\r') {
            raw.pop_back();
        }

        const bool isInclude =
            index < scanned.size() && LineIsAnInclude(scanned[index].code);
        if (isInclude) {
            std::string hoisted = TrimmedCopy(scanned[index].code);
            bool        already = false;
            for (const std::string& seen : outIncludes) {
                if (seen == hoisted) {
                    already = true;
                    break;
                }
            }
            if (!already) {
                outIncludes.push_back(std::move(hoisted));
            }
            outBody += '\n';   // the blank that keeps the numbering
        } else {
            outBody += raw;
            outBody += '\n';
        }

        if (end == text.size()) {
            break;
        }
        start = end + 1;
        ++index;
    }
}

// THE ONE LINE THAT IS THE WHOLE FEATURE, written in three places - at global
// scope for every script, and again inside each script's namespace to hide the
// class the using-directive for helpers drags in. One function so the three
// cannot drift: if they spelled different types, `Door door;` would mean one
// thing in one script and another in the next.
std::string AliasLine(const std::string& className) {
    std::string line = "using ";
    line += className;
    line += " = ::eng::ScriptRef<s_";
    line += className;
    line += "::";
    line += className;
    line += ">;\n";
    return line;
}

std::size_t LinesIn(const std::string& text) {
    std::size_t n = 1;
    for (const char c : text) {
        if (c == '\n') {
            ++n;
        }
    }
    return n;
}

// ===========================================================================
//  WHERE ONE CLASS IS DEFINED IN A PIECE OF TEXT, as an offset of its
//  `class` or `struct` keyword, or npos.
//
//  The same whitelist-parser caution as everywhere else in this file: it can
//  be fooled by the word in a comment or a string, so nothing it answers is
//  allowed to REFUSE a build. Both callers below only use it to shape
//  generated code or to offer an explanation, and the compiler stays the
//  authority on what the text actually means.
// ===========================================================================
std::size_t ClassDefinitionAt(std::string_view text, std::string_view name,
                              std::string* outKeyword = nullptr) {
    if (name.empty()) {
        return std::string::npos;
    }
    for (const std::string_view keyword : {"class ", "struct "}) {
        std::size_t at = text.find(keyword);
        while (at != std::string::npos) {
            // Whole word before it too, so `enum class Mode` and a type
            // called `subclass Door` cannot be mistaken for a definition.
            const bool startOfWord =
                at == 0 || !(std::isalnum(static_cast<unsigned char>(text[at - 1])) ||
                             text[at - 1] == '_');
            const std::size_t nameAt = text.find_first_not_of(" \t", at + keyword.size());
            if (startOfWord && nameAt != std::string::npos &&
                text.compare(nameAt, name.size(), name) == 0) {
                const std::size_t after = nameAt + name.size();
                const char        next  = after < text.size() ? text[after] : ' ';
                const bool wholeWord = !(std::isalnum(static_cast<unsigned char>(next)) ||
                                         next == '_');
                // Not a forward declaration: `class Door;` names the class
                // without defining it, and this question is about the body.
                if (wholeWord && next != ';') {
                    if (outKeyword != nullptr) {
                        *outKeyword = std::string(keyword.substr(0, keyword.size() - 1));
                    }
                    return at;
                }
            }
            at = text.find(keyword, at + 1);
        }
    }
    return std::string::npos;
}

} // namespace

// ===========================================================================
//  THE SAME TEXT WITH ITS COMMENTS BLANKED OUT, for the questions that are
//  about the CODE.
//
//  Found by this build asking itself a question and being answered by its own
//  documentation: Door.cpp explains in a comment that `doorSwitch->Method()`
//  is the one thing a mutual pair cannot do, and the scan for "does this
//  script reach through that reference" read the comment, decided it did, and
//  reported a loop of calls that the code does not contain. A note in a file
//  has to be able to quote the code it is about.
//
//  SO THE COMMENTS GO, AND NOTHING ELSE DOES. String and character literals
//  are tracked only so that a `//` inside one - a URL, a path - is not
//  mistaken for the start of a comment. Every character is replaced by a
//  space rather than removed, so offsets and line numbers still line up with
//  the original and anything reported about one is true of the other.
//
//  THE VARIABLE SCAN DOES NOT USE THIS, deliberately: tooltips and markers
//  are read FROM the comments - `// [0..10] how fast it turns` - so that one
//  needs the text as written. This is for the structural questions only.
// ===========================================================================
std::string StripComments(std::string_view source) {
    std::string out(source);

    enum class In { Code, Line, Block, Quote, Char } state = In::Code;

    for (std::size_t i = 0; i < out.size(); ++i) {
        const char c    = out[i];
        const char next = i + 1 < out.size() ? out[i + 1] : '\0';

        switch (state) {
        case In::Code:
            if (c == '/' && next == '/') {
                state  = In::Line;
                out[i] = out[i + 1] = ' ';
                ++i;
            } else if (c == '/' && next == '*') {
                state  = In::Block;
                out[i] = out[i + 1] = ' ';
                ++i;
            } else if (c == '"') {
                state = In::Quote;
            } else if (c == '\'') {
                state = In::Char;
            }
            break;

        case In::Line:
            // The newline stays, so the line count is untouched.
            if (c == '\n') {
                state = In::Code;
            } else {
                out[i] = ' ';
            }
            break;

        case In::Block:
            if (c == '*' && next == '/') {
                state  = In::Code;
                out[i] = out[i + 1] = ' ';
                ++i;
            } else if (c != '\n') {
                out[i] = ' ';
            }
            break;

        case In::Quote:
        case In::Char:
            // A BACKSLASH SKIPS THE NEXT CHARACTER, so "\"" does not look
            // like the end of the literal. Nothing in here is blanked - this
            // state exists only to not mistake a `//` inside a string for a
            // comment.
            if (c == '\\') {
                ++i;
            } else if ((state == In::Quote && c == '"') ||
                       (state == In::Char && c == '\'')) {
                state = In::Code;
            }
            break;
        }
    }
    return out;
}


std::string_view ScriptNameShadowsFieldType(std::string_view scriptName) {
    if (scriptName.empty()) {
        return {};
    }

    for (const TypeSpelling& spelling : kTypeSpellings) {
        // QUALIFIED SPELLINGS ARE SKIPPED, and skipping them is correct
        // rather than lazy: "eng::Color" and "std::string" cannot be a class
        // name - the colons are not identifier characters, so no script can
        // be called that and no alias can shadow them. It is the BARE
        // spellings beside them that a script name can collide with.
        if (spelling.spelling.find(':') != std::string_view::npos) {
            continue;
        }
        if (spelling.spelling == scriptName) {
            return spelling.spelling;
        }
    }
    return {};
}

// WHICH KEYWORD THE AUTHOR USED, because every script is now FORWARD-DECLARED
// before any of them is defined and a declaration that says `class` for a
// `struct` earns warning C4099 - about a line in a generated file, naming a
// difference the author cannot see from their own code.
std::string ClassKeyOf(std::string_view code, std::string_view className) {
    std::string keyword;
    if (ClassDefinitionAt(code, className, &keyword) != std::string::npos) {
        return keyword;
    }
    return "class";   // what every script written from the templates says
}

// ===========================================================================
//  ONE SCRIPT DERIVING FROM ANOTHER, which is the one thing the bare name
//  cannot be used for.
//
//  `Door door;` is a reference because the bare name IS a reference - and a
//  base class has to be the CLASS, so `class SlidingDoor : public Door` asks
//  to inherit from a handle. C++ has no way for one name to be both, so the
//  spelling for a base is `Door::ScriptType`, which ScriptRef declares for
//  exactly this (see scene/ScriptRef.h).
//
//  FOUND HERE FOR TWO REASONS. A base class must be COMPLETE, so the script
//  being derived from has to be wrapped first - this is an ordering edge like
//  any other. And somebody who writes the bare name gets
//  `static_assert failed: 'SlidingDoor must inherit from eng::ScriptBehaviour'`
//  from the registrar, which is true and says nothing about the fix; the
//  caller turns `qualified == false` into a line that does.
// ===========================================================================
// ===========================================================================
//  DOES THIS SCRIPT REACH THROUGH THAT REFERENCE - `doorSwitch->Open()` - as
//  opposed to merely holding it?
//
//  THE DISTINCTION THE ORDER TURNS ON, and it is a real one rather than a
//  nicety. Declaring `DoorSwitch doorSwitch;` needs DoorSwitch DECLARED,
//  which every script is before any of them is defined.
//
//  `doorSwitch->OpensOnce()` is different: that is ordinary member lookup in
//  DoorSwitch, done where it is written, and it needs the class FINISHED. So
//  a script that calls into another has to be wrapped after it, and a script
//  that only points at one does not care.
//
//  Measured rather than reasoned: with the reference declared and never
//  reached through, every unit compiles; add one `->` and that line alone
//  fails with `error C2027: use of undefined type`, while the `if` above it
//  is accepted.
//
//  ==========================================================================
//  WHAT "THE `if` ABOVE IT IS ACCEPTED" DEPENDS ON, which this used to state
//  as though it were a property of ScriptRef and is not.
//
//  It said testing a reference - `if (doorSwitch)` - goes through ScriptRef's
//  own members and so needs nothing complete. Only the first half is true.
//  operator bool calls Get(), and Get()'s body holds the static_assert that
//  asks whether T derives from ScriptBehaviour - which MSVC cannot answer for
//  an incomplete T: `error C2139: an undefined class is not allowed as an
//  argument to compiler intrinsic type trait '__is_base_of'`.
//
//  IT COMPILES HERE BECAUSE OF THE UNIT, NOT BECAUSE OF THE TYPE. Member
//  functions of a class template are instantiated at the END of the
//  translation unit, and every unit this build writes contains every script's
//  definition - so by the time operator bool is instantiated, DoorSwitch is
//  whole. That is what makes it safe, and it is a property of the one-unit-
//  carries-everything arrangement rather than of ScriptRef.
//
//  SO THIS FUNCTION IS RIGHT FOR THE QUESTION IT ANSWERS and would be wrong
//  for a different one. Ordering within a unit turns on `->` alone, which is
//  resolved where it is written. "Which definitions does this script need in
//  its unit at all" is a LARGER set - `->`, .Get(), and every boolean test -
//  and anything that splits the units has to compute that one instead. The
//  measured boundary:
//
//      compiles with T merely declared   the member itself, arrays of them,
//                                        IsSet, Clear, Set, TargetName, and
//                                        being exposed through f.Ref
//      needs T complete                  operator bool, Get(), ->
// ===========================================================================
bool ReachesThroughReference(std::string_view text, std::string_view member) {
    if (member.empty()) {
        return false;
    }
    std::size_t at = text.find(member);
    while (at != std::string::npos) {
        const bool startOfWord =
            at == 0 || !(std::isalnum(static_cast<unsigned char>(text[at - 1])) ||
                         text[at - 1] == '_');
        std::size_t after = at + member.size();
        if (startOfWord) {
            // `doorSwitch ->` is the same thing as `doorSwitch->`, and a
            // member named with a dot - doorSwitch.IsSet() - is not: that
            // reaches ScriptRef's own members and needs nothing complete.
            while (after < text.size() && (text[after] == ' ' || text[after] == '\t')) {
                ++after;
            }
            if (text.compare(after, 2, "->") == 0) {
                return true;
            }
        }
        at = text.find(member, at + 1);
    }
    return false;
}

bool ResolvesReference(std::string_view text, std::string_view typeName,
                       std::string_view member) {
    if (member.empty()) {
        return false;
    }

    std::size_t at = text.find(member);
    while (at != std::string::npos) {
        const std::size_t end = at + member.size();

        const bool wholeWord =
            (at == 0 || !IsIdentChar(text[at - 1])) &&
            (end >= text.size() || !IsIdentChar(text[end]));

        if (wholeWord) {
            // ---- is this the DECLARATION rather than a use? --------------
            //
            // `DoorSwitch doorSwitch;` names the member once without using
            // it, and counting that would make every held reference look
            // like a resolved one - which is the whole distinction.
            std::size_t before = at;
            while (before > 0 && (text[before - 1] == ' ' || text[before - 1] == '\t')) {
                --before;
            }
            const bool declaration =
                !typeName.empty() && before >= typeName.size() &&
                text.compare(before - typeName.size(), typeName.size(), typeName) == 0 &&
                (before == typeName.size() ||
                 !IsIdentChar(text[before - typeName.size() - 1]));

            if (!declaration) {
                std::size_t after = end;
                while (after < text.size() &&
                       (text[after] == ' ' || text[after] == '\t')) {
                    ++after;
                }

                // AN ELEMENT OF AN ARRAY IS STILL THE ELEMENT. `others[0]`
                // resolves nothing by itself; what follows the bracket is
                // what decides, exactly as for a plain member.
                if (after < text.size() && text[after] == '[') {
                    int depth = 0;
                    while (after < text.size()) {
                        if (text[after] == '[') {
                            ++depth;
                        } else if (text[after] == ']') {
                            --depth;
                            if (depth == 0) {
                                ++after;
                                break;
                            }
                        }
                        ++after;
                    }
                    while (after < text.size() &&
                           (text[after] == ' ' || text[after] == '\t')) {
                        ++after;
                    }
                }

                // ======================================================
                //  A DOT IS ALMOST ALWAYS SAFE, AND ONCE IS NOT.
                //
                //  ScriptRef<T> adds exactly three members to EntityRef:
                //  Get(), operator-> and operator bool. All three resolve
                //  the reference and so need T whole; everything else
                //  somebody can write after a dot - IsSet, Set, Clear,
                //  TargetName, Target, Id - is EntityRef's and is compiled
                //  against a name rather than against T.
                //
                //  So `.Get()` is the one dot that counts, and missing it
                //  would be the dangerous direction: a definition left out
                //  of a unit that needed it.
                // ======================================================
                if (after >= text.size() || text[after] != '.') {
                    return true;
                }

                std::size_t name = after + 1;
                while (name < text.size() &&
                       (text[name] == ' ' || text[name] == '\t')) {
                    ++name;
                }
                std::size_t nameEnd = name;
                while (nameEnd < text.size() && IsIdentChar(text[nameEnd])) {
                    ++nameEnd;
                }
                if (text.compare(name, nameEnd - name, "Get") == 0) {
                    return true;
                }
            }
        }
        at = text.find(member, at + 1);
    }
    return false;
}


// ===========================================================================
//  THE HALF info.vars CANNOT SEE: A MEMBER THE INSPECTOR NEVER GETS TO DRAW.
//
//  Both predicates above are asked about a member from the SCANNED VARIABLE
//  LIST, and that list is the list the Inspector will draw. A private member
//  is not in it - it was never a candidate - so a unit whose only use of
//  another script goes through one looked, to the closure that decides which
//  definitions a unit carries, like a unit that used nothing.
//
//  THE SYMPTOM WAS A MEMBER'S ACCESS LEVEL DECIDING WHETHER THE PROJECT
//  COMPILED, which is not a thing C++ has ever done. These two lines built:
//
//      public:  Invisibility invis;        invis->ToggleInvisibility();
//
//  and the same two under `private:` were
//
//      error C2027: use of undefined type 's_Invisibility::Invisibility'
//
//  because the generated unit carried the forward declaration and no body.
//  `private:` is what the script template tells people to write, so this was
//  reachable by following the instructions rather than by doing anything odd.
//
//  SO THE DECLARATIONS ARE READ OUT OF THE TEXT, at every access level, and
//  the same two predicates then run on them unchanged. Nothing here judges
//  what a USE means - that is still ResolvesReference's job and there is one
//  copy of it.
//
//  OVER-APPROXIMATING IS THE SAFE DIRECTION, exactly as it is above: a
//  parameter named like a member, or a type mentioned in a way this does not
//  understand, costs one unit a rebuild. Missing a declaration costs a
//  definition, and that is the C2027.
// ===========================================================================
std::vector<std::string> MembersOfScriptType(std::string_view text,
                                             std::string_view typeName) {
    std::vector<std::string> found;
    if (typeName.empty()) {
        return found;
    }

    std::size_t at = text.find(typeName);
    while (at != std::string::npos) {
        const std::size_t end = at + typeName.size();

        const bool wholeWord = (at == 0 || !IsIdentChar(text[at - 1])) &&
                               (end >= text.size() || !IsIdentChar(text[end]));
        if (wholeWord) {
            // `Door door;` and `Door door[4];`: the type, whitespace, then a
            // name. Everything else that mentions the type - a cast, a
            // template argument, `Door::ScriptType` - leaves no identifier
            // here and is left to the caller.
            std::size_t after = end;
            while (after < text.size() && (text[after] == ' ' || text[after] == '\t')) {
                ++after;
            }
            const std::size_t nameAt = after;
            while (after < text.size() && IsIdentChar(text[after])) {
                ++after;
            }
            if (after > nameAt) {
                std::string name(text.substr(nameAt, after - nameAt));
                if (std::find(found.begin(), found.end(), name) == found.end()) {
                    found.push_back(std::move(name));
                }
            }
        }
        at = text.find(typeName, at + 1);
    }
    return found;
}

// ===========================================================================
//  THE MISTAKE NOTHING USED TO MENTION: A REFERENCE ASSIGNED FROM Find.
//
//  `invis = Find<Invisibility>();` is the first thing somebody writes, because
//  it is the shape Unity's GetComponent has. Every part of it is wrong here -
//  Find hands back a POINTER, a reference is not assigned from one, and Find
//  cannot look a script up at all - and what the author used to get was a bare
//  C2679 whose only type name was `s_TheirScript::Invisibility`, a namespace
//  this build invented and that appears in no file they can open.
//
//  THE SCAN CAN SEE THIS, which is the point. It has the declaration and it
//  has the text, so it can say the one sentence the compiler cannot: that the
//  member is a reference and a reference is filled in differently.
//
//  NEVER A FALSE ALARM ON A REFERENCE. There is no assignment from a pointer
//  to EntityRef - that is exactly why the line fails - so a member this
//  answers true for is a member that cannot compile.
// ===========================================================================
bool AssignedFromFind(std::string_view text, std::string_view member) {
    if (member.empty()) {
        return false;
    }

    std::size_t at = text.find(member);
    while (at != std::string::npos) {
        const std::size_t end = at + member.size();

        const bool wholeWord = (at == 0 || !IsIdentChar(text[at - 1])) &&
                               (end >= text.size() || !IsIdentChar(text[end]));
        if (wholeWord) {
            std::size_t after = end;
            while (after < text.size() && (text[after] == ' ' || text[after] == '\t')) {
                ++after;
            }

            // A PLAIN `=`. Not `==`, which is a comparison, and not `+=` and
            // friends, which cannot be the first thing after the name.
            if (after < text.size() && text[after] == '=' &&
                (after + 1 >= text.size() || text[after + 1] != '=')) {
                const std::size_t      semi = text.find(';', after);
                const std::string_view rhs  = text.substr(
                    after + 1,
                    (semi == std::string::npos ? text.size() : semi) - after - 1);

                // `Find<` on the right, as a whole word - so FindEntity and
                // somebody's own NearestFind are not mistaken for it.
                std::size_t f = rhs.find("Find");
                while (f != std::string::npos) {
                    const bool  startsWord = f == 0 || !IsIdentChar(rhs[f - 1]);
                    std::size_t angle      = f + 4;
                    while (angle < rhs.size() &&
                           (rhs[angle] == ' ' || rhs[angle] == '\t')) {
                        ++angle;
                    }
                    if (startsWord && angle < rhs.size() && rhs[angle] == '<') {
                        return true;
                    }
                    f = rhs.find("Find", f + 1);
                }
            }
        }
        at = text.find(member, at + 1);
    }
    return false;
}

// ===========================================================================
//  A BARE COMPONENT NAME WITH A STAR, which is the one line that changed
//  meaning when engine components became references.
//
//  `Rigidbody2DComponent* body;` used to be a pointer to the component. The
//  bare name is an alias for eng::ComponentRef now - that is what makes
//  `SpriteComponent renderer;` a slot, the whole point of the change - so the
//  same line declares a pointer to a REFERENCE, and every use of it fails
//  somewhere else: at the assignment, and again at each `body->`.
//
//  NOTHING ELSE WOULD SAY SO. The member is usually private, so the scan that
//  feeds the Inspector never sees it and the author gets C2440 and a C2039
//  per call site, none of them on the declaration that is actually wrong.
//  This is the only place that reads the declaration.
// ===========================================================================
namespace {

// The engine's own hooks, which are called by name from elsewhere and are not
// actions. Kept beside the scanner rather than derived from ScriptHooks.h,
// because that header's concepts are types rather than a list and this is a
// text scan with no types in sight.
bool IsEngineHookName(std::string_view name) {
    static constexpr std::string_view kHooks[] = {
        "OnStart",          "OnUpdate",        "OnFixedUpdate",
        "OnDestroy",        "OnExpose",        "OnEnable",
        "OnDisable",        "OnTriggerEnter",  "OnTriggerStay",
        "OnTriggerExit",    "OnCollisionEnter", "OnCollisionStay",
        "OnCollisionExit",
    };
    for (const std::string_view hook : kHooks) {
        if (hook == name) {
            return true;
        }
    }
    return false;
}

} // namespace

std::vector<std::string> ActionHandlerDeclarations(std::string_view code) {
    std::vector<std::string> found;
    const std::string        text(code);

    std::size_t at = 0;
    while ((at = FindWord(text, "void", at)) != std::string::npos) {
        const std::size_t afterVoid = at + 4;
        at                          = afterVoid;

        std::size_t i = afterVoid;
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) {
            ++i;
        }

        // "On" FOLLOWED BY A CAPITAL. Unity upper-cases the first letter of
        // the action when it builds the method name, so every handler has
        // one - and requiring it is what keeps `OnePerSecond` and `Only`
        // from being read as handlers for "ePerSecond" and "ly".
        if (i + 2 >= text.size() || text[i] != 'O' || text[i + 1] != 'n') {
            continue;
        }
        const char third = text[i + 2];
        if ((third < 'A' || third > 'Z') && third != '_') {
            continue;
        }

        const std::size_t nameAt = i;
        while (i < text.size() && IsIdentChar(text[i])) {
            ++i;
        }
        const std::string name = text.substr(nameAt, i - nameAt);

        while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) {
            ++i;
        }
        if (i >= text.size() || text[i] != '(') {
            continue;
        }
        if (IsEngineHookName(name)) {
            continue;
        }

        // Declared and then defined is one handler, not two.
        if (std::find(found.begin(), found.end(), name) == found.end()) {
            found.push_back(name);
        }
        at = i;
    }

    return found;
}

std::vector<ComponentPointerDecl> ComponentPointerDeclarations(std::string_view code) {
    std::vector<ComponentPointerDecl> found;

    for (const TypeSpelling& spelling : kTypeSpellings) {
        // The bare component names are exactly the entries whose spelling IS
        // the component they constrain to. The aliases (SpriteRenderer and
        // the rest) are not affected: they were always references.
        if (spelling.required.empty() || spelling.spelling != spelling.required) {
            continue;
        }

        std::size_t at = code.find(spelling.spelling);
        while (at != std::string::npos) {
            const std::size_t end = at + spelling.spelling.size();

            const bool wholeWord =
                (at == 0 || !IsIdentChar(code[at - 1])) &&
                (end >= code.size() || !IsIdentChar(code[end]));

            // AND NOT QUALIFIED. `eng::SpriteComponent*` names the CLASS, so
            // a star on it is an ordinary raw pointer and entirely correct -
            // it is the escape hatch this whole message points people at.
            // Reporting it would be noise, and worse: it would tell an author
            // to "fix" the one spelling that did not need fixing. A colon is
            // not an identifier character, so the whole-word test above lets
            // it through on its own.
            const bool qualified =
                at >= 2 && code[at - 1] == ':' && code[at - 2] == ':';

            if (wholeWord && !qualified) {
                std::size_t after = end;
                while (after < code.size() &&
                       (code[after] == ' ' || code[after] == '\t')) {
                    ++after;
                }
                if (after < code.size() && code[after] == '*') {
                    ++after;
                    while (after < code.size() &&
                           (code[after] == ' ' || code[after] == '\t')) {
                        ++after;
                    }
                    const std::size_t nameAt = after;
                    while (after < code.size() && IsIdentChar(code[after])) {
                        ++after;
                    }
                    if (after > nameAt) {
                        ComponentPointerDecl decl;
                        decl.typeName   = std::string(spelling.spelling);
                        decl.memberName = std::string(code.substr(nameAt, after - nameAt));
                        found.push_back(std::move(decl));
                    }
                }
            }
            at = code.find(spelling.spelling, at + 1);
        }
    }
    return found;
}

// Is the script named through `Name::`, which in practice is `Name::ScriptType`
// - the one spelling that reaches the CLASS rather than a reference to it?
// Whatever takes it needs the definition: FindScriptOn dynamic_casts to it and
// a base clause derives from it, and both want it EARLIER in the unit.
//
// Without this, `eng::FindScriptOn<Other::ScriptType>(*Owner())` compiled only
// when the same file happened to declare an `Other`-typed member as well, so
// the advice to write it failed for a reason nothing explained.
bool NamesScriptTypeQualified(std::string_view text, std::string_view typeName) {
    if (typeName.empty()) {
        return false;
    }

    std::size_t at = text.find(typeName);
    while (at != std::string::npos) {
        const std::size_t end = at + typeName.size();
        if ((at == 0 || !IsIdentChar(text[at - 1])) && end + 1 < text.size() &&
            text[end] == ':' && text[end + 1] == ':') {
            return true;
        }
        at = text.find(typeName, at + 1);
    }
    return false;
}
std::vector<ScriptBaseUse> BaseScriptsOf(std::string_view                text,
                                         std::string_view                className,
                                         const std::vector<std::string>& names) {
    std::vector<ScriptBaseUse> found;

    const std::size_t at = ClassDefinitionAt(text, className);
    if (at == std::string::npos) {
        return found;
    }

    // From the class head to its opening brace, and no further: everything
    // after that is the body, where a script name means a member's type.
    const std::size_t brace = text.find('{', at);
    if (brace == std::string::npos) {
        return found;
    }
    const std::size_t colon = text.find(':', at);
    if (colon == std::string::npos || colon > brace) {
        return found;   // no base-clause at all
    }

    for (std::size_t i = colon; i < brace;) {
        if (!(std::isalpha(static_cast<unsigned char>(text[i])) || text[i] == '_')) {
            ++i;
            continue;
        }
        std::size_t end = i;
        while (end < brace && (std::isalnum(static_cast<unsigned char>(text[end])) ||
                               text[end] == '_')) {
            ++end;
        }
        const std::string word(text.substr(i, end - i));
        if (word != className &&
            std::find(names.begin(), names.end(), word) != names.end()) {
            ScriptBaseUse use;
            use.name = word;
            // `Door::ScriptType` - the spelling that works. Anything else
            // naming a script here is the bare one.
            use.qualified = text.compare(end, 2, "::") == 0;
            found.push_back(std::move(use));
        }
        i = end;
    }
    return found;
}


std::string GenerateScriptScopeHeader(const std::vector<ScriptUnitSource>& sources) {
    std::string out;
    out.reserve(16384);

    out += "#pragma once\n";
    out += "// Written by the editor, for your IDE and for nothing else. Safe to\n";
    out += "// delete: it is written again every time the scripts build.\n";
    out += "//\n";
    out += "// WHAT IT IS FOR. Your editor opens one script at a time, and on its own\n";
    out += "// a script has never heard of the others - so `Bullet b;` would be\n";
    out += "// underlined as an unknown type even though it compiles. This declares\n";
    out += "// every script in the project so that the bare name means what it means\n";
    out += "// in the real build: a reference you can drag an entity onto.\n";
    out += "//\n";
    out += "// IT IS NOT THE REAL COMPILE, and two differences are worth knowing.\n";
    out += "// Every script is complete here by the time your file is read, so two\n";
    out += "// scripts that each call the other show no error in the IDE and are\n";
    out += "// still refused by the build, which says so by name. And your file is\n";
    out += "// at global scope here rather than inside its own namespace, so another\n";
    out += "// script's file-scope helper functions are not offered to you while you\n";
    out += "// type even though they compile.\n";
    out += "//\n";
    out += "// See GenerateScriptScopeHeader in engine/tools/ScriptVars.h.\n";
    out += "\n";
    out += "#include <engine/Engine.h>\n";

    // ---- the scripts' own includes, hoisted exactly as a unit hoists them ---
    //
    // SAME REASON AS THERE, and it is not a style choice: an #include inside a
    // namespace puts whatever it names inside that namespace too. Measured in
    // this project - <cmath> survived only because the precompiled header had
    // already included it, and <bitset> in the same position gave sixty
    // errors.
    std::vector<std::string> includes;
    std::vector<std::string> bodies;
    bodies.reserve(sources.size());
    for (const ScriptUnitSource& src : sources) {
        std::string body;
        SplitOutIncludes(src.text, includes, body);
        bodies.push_back(std::move(body));
    }
    for (const std::string& line : includes) {
        // ENGINE_INCLUDE IS ALREADY ABOVE. #pragma once makes a second copy
        // harmless, and a generated file with an obviously duplicated line in
        // it still invites somebody to wonder which one is the mistake.
        if (line.find("engine/Engine.h") != std::string::npos) {
            continue;
        }
        out += line;
        out += '\n';
    }
    out += '\n';

    // ---- every script declared --------------------------------------------
    out += "// Every script declared, so that the names below can be formed before\n";
    out += "// any of the classes are finished.\n";
    for (const ScriptUnitSource& src : sources) {
        out += "namespace s_";
        out += src.className;
        out += " { ";
        out += src.classKey.empty() ? "class" : src.classKey;
        out += ' ';
        out += src.className;
        out += "; }\n";
    }
    out += '\n';

    // ==================================================================
    //  THE BARE NAMES, IN A NAMESPACE, AND THAT IS THE WHOLE TRICK.
    //
    //  At global scope - where GenerateScriptUnit puts them, because there
    //  each script's own text is inside its namespace and cannot collide -
    //  the alias for the script THIS file defines would be a second
    //  declaration of that name in the same scope. A redefinition, on the
    //  class the author is looking at.
    //
    //  Brought in by a using-directive instead, the alias is not a
    //  declaration in the global namespace at all: declaring `class Bullet`
    //  beside it is legal, the class parses, and lookup only objects if
    //  something USES the name where both are visible. A script does that
    //  once, on its ENGINE_REGISTER_SCRIPT line, which the redefinition at
    //  the bottom of this file takes care of.
    // ==================================================================
    out += "// The bare names. In a namespace rather than at global scope, so that the\n";
    out += "// script in the file you are editing can still declare its own class.\n";
    out += "namespace engine_script_names {\n";
    for (const ScriptUnitSource& src : sources) {
        out += AliasLine(src.className);
    }
    out += "} // namespace engine_script_names\n";
    out += "using namespace engine_script_names;\n\n";

    // ---- and every script's members ---------------------------------------
    out += "// And the classes themselves, so that `b->Fire()` knows what Fire is.\n";
    for (std::size_t i = 0; i < sources.size(); ++i) {
        const ScriptUnitSource& src = sources[i];

        out += "namespace s_";
        out += src.className;
        out += " {\n";

        // #line, SO THAT AN ERROR IN SOMEBODY ELSE'S SCRIPT NAMES THEIR FILE
        // rather than this one. A copy of a script is still that script: if
        // Bullet.cpp is mid-edit and broken, the IDE should say Bullet.cpp,
        // which is a file the author can open and fix - not a generated
        // header they have never seen and cannot change.
        out += "#line 1 \"";
        out += src.realPath;
        out += "\"\n";
        out += bodies[i];

        out += "#line ";
        out += std::to_string(LinesIn(out) + 1);
        out += " \"";
        out += "<script scope>";
        out += "\"\n";
        out += "} // namespace s_";
        out += src.className;
        out += "\n\n";
    }

    // ==================================================================
    //  AND THE ONE LINE OF YOUR OWN FILE THIS ARRANGEMENT WOULD BREAK.
    //
    //  `ENGINE_REGISTER_SCRIPT(Bullet)` is the only place a script writes
    //  its own name unqualified at namespace scope - and here that name is
    //  both the class the file defines and a reference brought in above, so
    //  it is the one ambiguity this header creates. Qualified, it is the
    //  class, unambiguously: ::Bullet is a member of the global namespace,
    //  and qualified lookup only consults a using-directive when it finds
    //  nothing of its own.
    //
    //  The registrar object goes with it, which costs nothing: an IDE never
    //  links and never runs. Every static_assert survives, because they are
    //  the half of that macro worth having while you type - see
    //  ENGINE_SCRIPT_CHECKS in scene/ScriptHooks.h.
    // ==================================================================
    out += "// YOUR registrar line, which is the only place a script names itself\n";
    out += "// unqualified - and the one thing above that would make it ambiguous.\n";
    out += "// Qualified it is the class you defined. The checks all still run; only\n";
    out += "// the registrar object is left out, and an IDE never links.\n";
    out += "#undef ENGINE_REGISTER_SCRIPT\n";
    out += "#define ENGINE_REGISTER_SCRIPT(Type) ENGINE_SCRIPT_CHECKS(::Type)\n";

    // ======================================================================
    //  AND THE CATCH-ALL IS RELAXED FOR THIS PATH, deliberately and only
    //  here.
    //
    //  This header is force-included into the author's own .cpp, which is
    //  then compiled on its own - by the IDE for its squiggles, and by the
    //  game_scripts type-check target. The file about to be compiled is not
    //  named anywhere in here, so this header cannot say whether THAT
    //  script is one of the ones with action handlers. It knows only that
    //  some of them are.
    //
    //  Given a choice between refusing a script that is fine and accepting
    //  one that is empty, it takes the second - which is this codebase's
    //  standing rule about generated code, applied where the generator
    //  genuinely cannot tell. Nothing is lost: the real build compiles the
    //  generated UNIT, where the bracketing around each body makes the same
    //  check exact, and a script with no hooks at all is still refused
    //  there and reported again at load time.
    // ======================================================================
    out += "#undef ENGINE_SCRIPT_MAYBE_ACTIONS\n";
    out += "#define ENGINE_SCRIPT_MAYBE_ACTIONS true\n";
    return out;
}

std::string GenerateScriptUnit(const ScriptSourceInfo&              info,
                               const std::vector<ScriptUnitSource>& sources,
                               std::string_view                     unitPath) {
    std::string out;
    out.reserve(8192);

    out += "// Written by the editor. Do not edit, and do not commit: it is\n";
    out += "// regenerated from the scripts' own sources every time it builds.\n";
    out += "//\n";
    out += "// EACH SCRIPT IS WRAPPED IN A NAMESPACE OF ITS OWN, and the bare name\n";
    out += "// is then an alias for a reference to it. Inside s_Door the name Door\n";
    out += "// is the class, so that file compiles exactly as written and its\n";
    out += "// ENGINE_REGISTER_SCRIPT still names the class. Out here the same name\n";
    out += "// is eng::ScriptRef<s_Door::Door>, so every OTHER script that writes\n";
    out += "// `Door door;` gets a reference it can drag an entity onto - with no\n";
    out += "// header to write and no include to remember.\n";
    out += "//\n";
    out += "// THE NAMESPACE IS THE SAME IN EVERY UNIT, including the script's own,\n";
    out += "// because resolving a reference ends in dynamic_cast and MSVC matches\n";
    out += "// type_info by DECORATED NAME. ::Door and s_Door::Door are different\n";
    out += "// names, so a library that did not wrap the class would hand out\n";
    out += "// objects no other library could cast to.\n";
    out += "//\n";
    out += "// The #include lines are hoisted above every namespace because an\n";
    out += "// include INSIDE one puts whatever it names in there too. Each leaves a\n";
    out += "// blank line behind, so the #line directives stay true and a compiler\n";
    out += "// error still points at the author's file and line - and __FILE__ is\n";
    out += "// still the real path, which ScriptRegistry::Register compares by.\n";
    out += "//\n";
    out += "// See GenerateScriptUnit in engine/tools/ScriptVars.h.\n";
    out += "#include <cstddef>\n";
    out += "#include <iterator>\n";
    out += "#include <string>\n";
    out += "#include <type_traits>\n";

    // ONLY WHAT IS ACTUALLY DEFINED HERE contributes its includes. A script
    // left out of this unit contributes a declaration and an alias, and
    // neither of those needs a header - hoisting its includes would make
    // every unit pay for every script's headers, which is most of what
    // leaving a script out is meant to save.
    std::vector<std::string> includes;
    std::vector<std::string> bodies;
    bodies.reserve(sources.size());
    for (const ScriptUnitSource& src : sources) {
        std::string body;
        if (src.defineHere) {
            SplitOutIncludes(src.text, includes, body);
        }
        bodies.push_back(std::move(body));
    }

    for (const std::string& line : includes) {
        out += line;
        out += '\n';
    }
    out += '\n';

    // ====================================================================
    //  EVERY SCRIPT DECLARED, AND EVERY BARE NAME ALIASED, BEFORE ANY OF
    //  THEM IS DEFINED. This is what makes the feature general rather than
    //  true of one example.
    //
    //  A reference does not need the class it points at to be COMPLETE: a
    //  ScriptRef<T> is a NAME, inherited from EntityRef, so its layout is
    //  the same whatever T is, and the only member that needs T whole is
    //  Get() - instantiated where the reference is USED. (That is why
    //  ScriptRef.h keeps its static_assert in Get() rather than in the class
    //  body; the note there has the error it used to give.)
    //
    //  So a forward declaration is enough to form the alias, every script
    //  can name every other one in any direction, and TWO SCRIPTS THAT POINT
    //  AT EACH OTHER are no harder than two that do not. Writing the alias
    //  after each namespace instead - which is what this did first - meant
    //  "only the ones wrapped earlier", and no order puts both of a mutual
    //  pair first. It failed as C3646 on the author's own line, with nothing
    //  to connect it to an ordering rule they were never told about.
    //
    //  The namespaces have to be declared here anyway, for the
    //  using-directives below to be able to name them.
    // ====================================================================
    out += "// Every script declared, and every bare name aliased, before any of\n";
    out += "// them is defined - so a reference may point in either direction, or\n";
    out += "// both. A ScriptRef<T> is a name and needs no complete T.\n";
    for (const ScriptUnitSource& src : sources) {
        out += "namespace s_";
        out += src.className;
        out += " { ";
        out += src.classKey.empty() ? "class" : src.classKey;
        out += ' ';
        out += src.className;
        out += "; }\n";
    }
    out += '\n';
    for (const ScriptUnitSource& src : sources) {
        out += AliasLine(src.className);
    }
    out += '\n';

    for (std::size_t i = 0; i < sources.size(); ++i) {
        const ScriptUnitSource& src = sources[i];

        // ==================================================================
        //  DECLARED ABOVE, DEFINED HERE ONLY IF THIS UNIT NEEDS IT.
        //
        //  Its namespace, its forward declaration and its alias are already
        //  written, so every other script in this unit can still HOLD a
        //  reference to it, set one, test whether one was filled in, and have
        //  it drawn in the Inspector. What it cannot do is RESOLVE one - `->`,
        //  .Get(), or a boolean test - and ResolvesReference is what put this
        //  script into the unit's set when something here does.
        //
        //  LEAVING IT OUT IS THE WHOLE POINT. Every unit used to carry every
        //  script's body, so editing any one of them invalidated all of them.
        //  Carrying only what is reached is what makes one edit rebuild one
        //  library. See ScriptUnitSource::defineHere.
        // ==================================================================
        if (!src.defineHere) {
            continue;
        }

        out += "namespace s_";
        out += src.className;
        out += " {\n";

        // ==================================================================
        //  WHAT MAKES ANOTHER SCRIPT'S FILE-SCOPE HELPERS REACHABLE.
        //
        //  AND IT ONLY REACHES WHAT IS DEFINED IN THIS UNIT. A using-directive
        //  naming a namespace that holds nothing but a class declaration is
        //  legal and finds nothing, so a call to a helper in a script this
        //  unit left out is "identifier not found" at the author's own line.
        //  The editor keeps any script with file-scope code in every unit for
        //  exactly that reason; see hasFileScopeCode in ScriptBuild.cpp.
        //
        //  Wrapping each script in a namespace of its own is what lets the
        //  bare name be a reference, but it also hid every free function and
        //  constant a script declares at file scope - so two scripts that
        //  wanted one `IsPickup` still each needed their own copy, which is
        //  the duplication this was supposed to remove.
        //
        //  A using-directive per script fixes that. The second line is the
        //  part that is easy to miss: the directive also exposes the other
        //  script's CLASS, which would then be ambiguous against the alias at
        //  global scope - and worse, could win. Re-declaring the alias HERE
        //  hides both, because a declaration in this namespace beats anything
        //  a using-directive brought in. So `Door door;` stays a reference.
        //
        //  IT REACHES BACKWARDS ONLY, and that is a real limit rather than an
        //  oversight. Unqualified lookup finds what was declared BEFORE the
        //  point of use, so a script can call a helper from one wrapped
        //  earlier - which is to say from something it depends on - and a
        //  call forwards fails with an ordinary "identifier not found" at the
        //  author's own line. A helper several scripts share is better off in
        //  a .cpp of its own, which the build compiles once and links into
        //  every library at global scope, with no ordering rule at all.
        //
        //  A REFERENCE IS NOT LIMITED THAT WAY, and the difference is worth
        //  being clear about: the alias is re-declared for EVERY other script
        //  here, forwards and backwards alike, because the class it names was
        //  declared at the top of the file. Only the helpers depend on where
        //  a script sits in the order.
        // ==================================================================
        for (const ScriptUnitSource& other : sources) {
            if (other.className == src.className) {
                continue;
            }
            out += "using namespace s_";
            out += other.className;
            out += ";\n";
        }
        for (const ScriptUnitSource& other : sources) {
            if (other.className == src.className) {
                continue;
            }
            out += AliasLine(other.className);
        }

        // NOTHING HERE FOR THE ENGINE'S OWN COMPONENTS, and that is the
        // point. Their aliases are members of ScriptBehaviour, which every
        // script inherits - see scene/ComponentAliases.h - so they are
        // already in scope inside the class without this file writing a
        // line. They were emitted here for one version of this feature; the
        // cost was that a script read on its own said something different
        // from the unit, and every IDE underlined working code.

        // ==================================================================
        //  TELL ENGINE_SCRIPT_CHECKS THAT THIS ONE HAS HANDLERS.
        //
        //  The catch-all in ScriptHooks.h refuses a script with none of the
        //  fixed lifecycle hooks, and a script whose only members are
        //  void OnJump() and void OnMove(eng::Vec2) has none of them - it is
        //  driven entirely by a Player Input component. C++20 cannot
        //  enumerate members to discover that, so this says so instead.
        //
        //  AROUND THE BODY AND NOT INSIDE IT, and set back to false
        //  afterwards, because the next script wrapped in this same file
        //  must be judged on its own. BEFORE the #line directive, so the
        //  author's own line numbering is untouched and a diagnostic still
        //  points at the line they wrote.
        // ==================================================================
        //  EVERY BODY IS BRACKETED, with the true answer either way - not just
        //  the ones that have handlers. The macro defaults to PERMISSIVE
        //  (ScriptHooks.h says why: every path without a generator has no
        //  information and a strict default there refuses valid code), so this
        //  is the only place that can be strict. Saying "false" around a
        //  script that really has no handlers is what keeps the catch-all
        //  working for the build that actually ships.
        const std::vector<std::string> handlers = ActionHandlerDeclarations(src.text);
        out += "#undef ENGINE_SCRIPT_MAYBE_ACTIONS\n";
        out += handlers.empty() ? "#define ENGINE_SCRIPT_MAYBE_ACTIONS false\n"
                                : "#define ENGINE_SCRIPT_MAYBE_ACTIONS true\n";

        out += "#line 1 \"";
        out += src.realPath;
        out += "\"\n";
        out += bodies[i];

        // BACK TO THIS FILE BEFORE THE GENERATED LINES. Without the reset the
        // closing brace and the alias below are attributed to the author's
        // last line, and any diagnostic about them accuses a file that does
        // not contain them.
        out += "#line ";
        out += std::to_string(LinesIn(out) + 1);
        out += " \"";
        out += unitPath;
        out += "\"\n";

        // Back to the header's default, so the next body in this same file is
        // judged on its own answer rather than on this one's.
        out += "#undef ENGINE_SCRIPT_MAYBE_ACTIONS\n";
        out += "#define ENGINE_SCRIPT_MAYBE_ACTIONS true\n";

        // ==================================================================
        //  THE INPUT ACTION HANDLERS, INSIDE THIS SCRIPT'S OWN NAMESPACE.
        //
        //  HERE AND NOWHERE ELSE, and the site is the whole of why this
        //  works. Two earlier shapes were tried and both are broken:
        //
        //    APPENDED TO THE EXPOSER - which is emitted only when
        //    `!info.vars.empty()`, so a script whose only member is
        //    void OnPause() gets no exposer, no dispatcher, and silence.
        //    Exactly the scripts the feature is for.
        //
        //    AS A TRAIT SPECIALIZATION READ BY ENGINE_SCRIPT_CHECKS - which
        //    cannot name the class in both build paths. The scope header
        //    redefines the macro to ENGINE_SCRIPT_CHECKS(::Type) and the
        //    author's file is compiled at global scope, so the class there is
        //    ::PauseMenu while here it is s_PauseMenu::PauseMenu. A
        //    specialization on one does not cover the other, and the global
        //    cannot be forward-declared without making every cross-script
        //    bare name ambiguous.
        //
        //  This spot has neither problem: it runs once per BODY, after the
        //  class is complete, inside the namespace that names it - and the
        //  scope header never emits it at all, because an IDE has no use for
        //  a dispatcher.
        //
        //  A TEMPLATE, so the requires-ladders in ENGINE_CALL_ACTION_HANDLER
        //  sit in a dependent context where a failed requirement is
        //  unambiguously "no" rather than an error.
        // ==================================================================
        // (already scanned above, where the body is bracketed)
        if (!handlers.empty()) {
            out += "\nnamespace {\n";
            out += "constexpr const char* kActionHandlerNames[] = {";
            for (std::size_t h = 0; h < handlers.size(); ++h) {
                out += (h == 0) ? "\"" : ", \"";
                out += handlers[h];
                out += "\"";
            }
            out += "};\n";

            // WHICH OF THEM THE COMPILER CAN ACTUALLY REACH. The scanner is
            // allowed to guess, so the names above are candidates; this is
            // the answer for each one, worked out by the same three-shape
            // test the call ladder uses. A private method, or one taking
            // something no action can supply, comes out false here and is
            // never claimed as a handler at load time.
            out += "constexpr bool kActionHandlerBound[] = {";
            for (std::size_t h = 0; h < handlers.size(); ++h) {
                out += (h == 0) ? "" : ", ";
                out += "ENGINE_ACTION_HANDLER_BINDS(";
                out += src.className;
                out += ", ";
                out += handlers[h];
                out += ")";
            }
            out += "};\n\n";

            out += "template <class T>\n";
            out += "void DispatchAction(::eng::ScriptBehaviour& behaviour, "
                   "std::size_t index,\n";
            out += "                    const ::eng::ActionEvent& event) {\n";
            out += "    T& self = static_cast<T&>(behaviour);\n";
            out += "    (void)self;\n";
            out += "    (void)event;\n";
            out += "    switch (index) {\n";
            for (std::size_t h = 0; h < handlers.size(); ++h) {
                out += "        case " + std::to_string(h) + ":\n";
                out += "            ENGINE_CALL_ACTION_HANDLER(self, " + handlers[h] +
                       ", event);\n";
                out += "            break;\n";
            }
            out += "        default: break;\n";
            out += "    }\n";
            out += "}\n\n";

            out += "const bool kActionHandlersRegistered =\n";
            out += "    ::eng::ScriptRegistry::SetActionHandlers(\"";
            out += src.className;
            out += "\", kActionHandlerNames,\n";
            out += "                                             kActionHandlerBound,\n";
            out += "                                             ";
            out += std::to_string(handlers.size());
            out += ", &DispatchAction<";
            out += src.className;
            out += ">);\n";
            out += "} // namespace\n";
        }

        // AND NO ALIAS AFTER IT ANY MORE: it is already at the top of the
        // file, with every other script's, which is what lets a reference
        // point forwards as well as back.
        out += "} // namespace s_";
        out += src.className;
        out += "\n\n";
    }

    // ONLY WHEN THERE IS SOMETHING TO EXPOSE, and the rule lives here rather
    // than at the three call sites that used to each repeat it. A script with
    // a hand-written OnExpose must not be given a generated one as well - the
    // hand-written list is the one that wins - and a script with no variables
    // gains nothing from an empty lambda.
    if (!info.hasOnExpose && !info.vars.empty()) {
        out += GenerateExposerFor(info, "s_" + info.className + "::" + info.className);
    }
    return out;
}

} // namespace eng
