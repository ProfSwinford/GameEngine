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
};

constexpr std::array kTypeSpellings{
    TypeSpelling{"eng::EntityRef", ScriptFieldType::Entity},
    TypeSpelling{"EntityRef", ScriptFieldType::Entity},
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
//  The trailing comment: markers, then the tooltip
//
//  A bracket group AT THE VERY START is a marker. Only at the start, so that
//  ordinary prose mentioning "[1]" is left alone - a comment is documentation
//  first and a declaration second.
// ---------------------------------------------------------------------------
void ReadComment(std::string_view comment, ScriptVarDecl& into) {
    std::string rest = TrimmedCopy(comment);

    if (rest.starts_with('[')) {
        const std::size_t close = rest.find(']');
        if (close != std::string::npos) {
            const std::string marker = TrimmedCopy(rest.substr(1, close - 1));
            rest                     = TrimmedCopy(rest.substr(close + 1));

            // "0..10" - a range, for a number.
            if (const std::size_t dots = marker.find(".."); dots != std::string::npos) {
                const std::string lowText  = TrimmedCopy(marker.substr(0, dots));
                const std::string highText = TrimmedCopy(marker.substr(dots + 2));

                // std::from_chars rather than std::stof: it does not throw, and
                // a marker somebody typed wrong should leave the variable alone
                // rather than take the whole build down.
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
                // Anything else is a type name, which only means something to
                // a reference. ScriptFields::Ref reports one that names
                // nothing registered, so it is not checked here.
                into.requiredType = marker;
            }
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
bool ReadDeclaration(const SourceLine& line, ScriptVarDecl& outVar,
                     ScriptVarSkipped& outSkipped, bool& outWasSkipped) {
    outWasSkipped = false;

    std::string code = line.code;
    if (code.empty() || !code.ends_with(';')) {
        return false;   // a declaration is one line and ends in a semicolon
    }
    code.pop_back();

    // ======================================================================
    //  AN m_ PREFIX MEANS "NOT FOR THE INSPECTOR", silently.
    //
    //  This project names every internal member m_something, everywhere, and
    //  that convention is already a statement of intent - it is this engine's
    //  [HideInInspector] and it was here first.
    //
    //  So one of them in a public section is left out AND left unreported.
    //  Reporting it would be noise on every single build: TilemapWalker keeps
    //  `Ridigbody2D m_body` in its public section, which is a typedef for a
    //  POINTER - so the parser cannot even see that it is one, and would
    //  complain about the type instead for as long as the file existed.
    //
    //  It is also the only way to hide a variable WITHOUT making it private,
    //  which matters for something a sibling class or a test needs to reach.
    // ======================================================================
    // The declared name is the LAST identifier before any initialiser - so
    // the part before '=' or '{', with the trailing identifier taken off it.
    // Looking at the end of the whole line instead would read `1.0f` out of
    // `float m_speed = 1.0f` and never match anything.
    {
        const std::size_t initAt = code.find_first_of("={");
        std::string       declPart =
            (initAt == std::string::npos) ? code : code.substr(0, initAt);

        const std::size_t end = declPart.find_last_not_of(" \t");
        if (end != std::string::npos && IsIdentChar(declPart[end])) {
            std::size_t start = end;
            while (start > 0 && IsIdentChar(declPart[start - 1])) {
                --start;
            }
            if (declPart.compare(start, 2, "m_") == 0) {
                return false;
            }
        }
    }

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

        outSkipped.name   = declarator;
        outSkipped.reason = "its type '" + typeText +
                            "' is not one the Inspector can draw - it handles float, "
                            "int, bool, std::string, Vec2, Color and EntityRef";
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
    const std::string tail = TrimmedCopy(afterType.substr(nameEnd));

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

    outVar      = ScriptVarDecl{};
    outVar.name = name;
    outVar.type = std::string(matched->spelling);
    outVar.fieldType = matched->type;
    ReadComment(line.comment, outVar);

    // A range only means something on a number, and a required type only on a
    // reference. Dropped rather than carried, so nothing downstream has to ask
    // whether it applies.
    if (outVar.fieldType != ScriptFieldType::Float &&
        outVar.fieldType != ScriptFieldType::Int) {
        outVar.ranged = false;
    }
    if (outVar.fieldType != ScriptFieldType::Entity) {
        outVar.requiredType.clear();
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
                SourceLine candidate{code, line.comment};
                ScriptVarDecl    found;
                ScriptVarSkipped skipped;
                bool             wasSkipped = false;

                if (ReadDeclaration(candidate, found, skipped, wasSkipped)) {
                    info.vars.push_back(std::move(found));
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
        // ================================================================
        const std::string call = var.ranged ? "Range" : (var.requiredType.empty() ? "Var" : "Ref");

        std::string args = "\"" + Escaped(var.name) + "\", s->" + var.name;
        if (var.ranged) {
            args += ", " + BoundLiteral(var.fieldType, var.min);
            args += ", " + BoundLiteral(var.fieldType, var.max);
        } else if (!var.requiredType.empty()) {
            args += ", \"" + Escaped(var.requiredType) + "\"";
        }

        out += "                []<class S>(S* s, ::eng::ScriptFields& f) {\n";
        out += "                    if constexpr (requires { f." + call + "(" + args +
               "); }) {\n";
        out += "                        f." + call + "(" + args;
        if (!var.tooltip.empty()) {
            out += ", \"" + Escaped(var.tooltip) + "\"";
        }
        out += ");\n";
        out += "                    }\n";
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
