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
    // ---- Unity's component names, which are REFERENCES here as they are
    //      reference types in C#. See scene/ComponentRef.h.
    // `Transform` is impossible as a field type - ScriptBehaviour::Transform()
    // hides it - and Collider2D is a pointer to an abstract base that
    // Entity::Find cannot look up. Engine.h argues both at length.
    TypeSpelling{"TransformRef", ScriptFieldType::Entity, "TransformComponent"},
    TypeSpelling{"SpriteRenderer", ScriptFieldType::Entity, "SpriteComponent"},
    TypeSpelling{"Rigidbody2D", ScriptFieldType::Entity, "Rigidbody2DComponent"},

    TypeSpelling{"eng::EntityRef", ScriptFieldType::Entity},
    TypeSpelling{"EntityRef", ScriptFieldType::Entity},
    TypeSpelling{"eng::PrefabRef", ScriptFieldType::Prefab},
    TypeSpelling{"PrefabRef", ScriptFieldType::Prefab},
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
    //  THE LONG SPELLING, eng::ComponentRef<eng::SpriteComponent>.
    //
    //  A PREFIX, so the table above cannot hold it: every entry there is an
    //  exact word match. The component is lifted out of the template argument
    //  textually and becomes the constraint, which is the one place in this
    //  file where a type name is read rather than recognised.
    //
    //  NO REGISTRY IS CONSULTED AND NONE COULD BE - this parser is pure, and
    //  the exposer is generated with no scene and no components registered.
    //  ScriptFields::AddRef already warns about a constraint naming nothing,
    //  in the one place that can actually answer the question.
    //
    //  The synthetic entry carries the WHOLE spelling, so the substr below
    //  steps past `ComponentRef<...>` without this branch having to know how.
    // ======================================================================
    std::string  componentRefSpelling;
    std::string  componentRefArgument;
    TypeSpelling synthetic{};
    if (matched == nullptr) {
        for (const std::string_view prefix :
             {std::string_view("eng::ComponentRef"), std::string_view("ComponentRef")}) {
            if (!WordAt(code, 0, prefix)) {
                continue;
            }
            const std::size_t open = code.find('<', prefix.size());
            if (open == std::string::npos) {
                break;
            }
            const std::size_t close = code.rfind('>');
            if (close == std::string::npos || close < open) {
                break;
            }
            componentRefArgument = TrimmedCopy(code.substr(open + 1, close - open - 1));
            if (componentRefArgument.starts_with("eng::")) {
                componentRefArgument.erase(0, 5);
            }
            if (componentRefArgument.empty()) {
                break;
            }
            componentRefSpelling = code.substr(0, close + 1);
            synthetic = TypeSpelling{componentRefSpelling, ScriptFieldType::Entity,
                                     componentRefArgument};
            matched   = &synthetic;
            break;
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
        //  AND A POINTER NEED NOT HAVE A STAR AT ALL. Engine.h hands scripts
        //  Unity-shaped aliases that are already pointers - Rigidbody2D,
        //  Collider2D, SpriteRenderer, GameObject - so
        //  `Rigidbody2D m_body = nullptr;` is correct, and
        //  assets/scripts/TilemapWalker.cpp writes exactly that. Initialising
        //  from nullptr is the one thing a value cannot do, which tells the
        //  two apart without this parser having to know what any of those
        //  names mean. It stays pure: no registry, no engine state. See
        //  ScriptVars.h.
        // ================================================================
        const bool starOnType =
            !typeText.empty() && (typeText.back() == '*' || typeText.back() == '&');
        const bool starOnName =
            !nameText.empty() && (nameText.front() == '*' || nameText.front() == '&');
        const std::size_t assign = nameText.find('=');
        const bool fromNullptr =
            assign != std::string::npos &&
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
                ", and the Inspector edits values. For a component on ANOTHER "
                "entity, that you drag an entity onto, the type to write is a "
                "reference - TransformRef, SpriteRenderer, Rigidbody2D, or "
                "eng::ComponentRef<TheComponent> for the rest; see "
                "scene/ComponentRef.h. For one on THIS entity, which is what a "
                "pointer beside a script usually is, ask with Find<T>() where you "
                "use it";
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
        outSkipped.name   = declarator;
        outSkipped.reason = "its type '" + typeText +
                            "' is not one the Inspector can draw - it handles float, "
                            "int, bool, std::string, Vec2, Color, EntityRef, "
                            "PrefabRef, ImageRef and AnimationRef. If '" + typeText +
                            "' is a component, this declares one by value, which is "
                            "not how a script holds one - ask for it with Find<eng::" +
                            typeText + ">() where you use it";
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

ScriptSourceInfo ScanScriptSource(std::string_view source) {
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

                if (ReadDeclaration(candidate, commentAbove, found, skipped,
                                    wasSkipped)) {
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

std::string GenerateExposerUnit(const ScriptSourceInfo& info,
                                std::string_view includePath) {
    std::string out;
    out.reserve(1024 + info.vars.size() * 256);

    out += "// Written by the editor. Do not edit, and do not commit: it is\n";
    out += "// regenerated from the script's own source every time it builds.\n";
    out += "//\n";
    out += "// It INCLUDES the script rather than copying it, so every line number,\n";
    out += "// every __FILE__ and the file's own anonymous namespace stay exactly as\n";
    out += "// they were - a compiler error still points at the line you wrote.\n";
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
    out += info.className;
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
        //  to be inaccessible, COMPILES AWAY instead of failing. It is then
        //  absent from the Inspector and present in the skipped report.
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

    out += "            });\n    }\n};\n";
    out += "const EngineAutoExpose_";
    out += info.className;
    out += " g_engineAutoExpose_";
    out += info.className;
    out += ";\n} // namespace\n";
    return out;
}

} // namespace eng
