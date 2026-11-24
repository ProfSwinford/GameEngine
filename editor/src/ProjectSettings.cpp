// ============================================================================
//  ProjectSettings.cpp - reading and writing config/engine.json. See the
//  header for the project-versus-person split and why this merges.
// ============================================================================

#include "ProjectSettings.h"

#include <engine/Engine.h>
#include <engine/core/Json.h>
#include <engine/core/Log.h>
#include <engine/fs/FileSystem.h>
#include <engine/render/Presentation.h>

#include <cmath>
#include <exception>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace editor {
namespace {

constexpr const char* kConfigPath = "config/engine.json";

// ============================================================================
//  WHAT EACH SECTION SAYS ABOUT ITSELF.
//
//  config/engine.json explains itself to whoever opens it - every hand-written
//  section carries a "_note" saying what the settings in it are for. A section
//  the EDITOR added should not be the one bare exception, so when a writer has
//  to create one it brings its note with it.
//
//  Kept in one table rather than passed in by each caller, so that two places
//  setting two keys of the same section cannot write two different
//  explanations of it. Written ONCE, when the section is created; an existing
//  section's note is never touched, because whoever wrote it meant it.
// ============================================================================
const char* NoteForSection(std::string_view section) {
    if (section == "presentation") {
        return "What shape the finished game is. \"free\" lets it fill the window, "
               "so a wider monitor sees MORE OF THE LEVEL. A ratio like \"16:9\" "
               "pins the shape; a size like \"1920x1080\" pins the exact pixels. "
               "Whatever space is left over is filled with BLACK. Set from the Game "
               "view's dropdown, or from Project Settings.";
    }
    if (section == "window") {
        return "The size the window opens at and what is in its title bar. "
               "\"resizable\": false locks the BUILT GAME's window to the width and "
               "height here - the editor's own window is never affected.";
    }
    if (section == "logging") {
        return "\"threshold\" hides anything less important than the level you name: "
               "Info, Warning or Error. \"file\" is where a copy of everything is "
               "written, so it survives a crash.";
    }
    if (section == "tunables") {
        return "Numbers worth changing without a rebuild. "
               "\"fixedTimestepSeconds\" is how much time one simulation step "
               "represents - this is NOT the frame rate, which is separate.";
    }
    if (section == "units") {
        return "How big ONE of something is, in pixels - one tile, one standing "
               "character's width, one grid square. Collider sizes are written as "
               "fractions of it, so they still mean the right thing after the art is "
               "redrawn at a different resolution.";
    }
    if (section == "physics") {
        return "\"gravity\" is in YOUR GAME'S UNITS, and this engine works in "
               "pixels. Unity's -9.81 is metres per second squared; at roughly 100 "
               "pixels to the metre, -981 is the same real-world gravity. Use [0, 0] "
               "for a top-down game.";
    }
    if (section == "startup") {
        return "The scene opened when the game starts. The build list wins over it "
               "when there is one - see the Build panel.";
    }
    return "";
}

// ============================================================================
//  WHY THIS SPLICES TEXT INSTEAD OF WRITING THE DOCUMENT BACK OUT
//
//  config/engine.json is written and maintained by hand. It has blank lines
//  between its sections, a "_note" against most of them explaining what the
//  setting is for, and the key bindings laid out in columns so a person can
//  read down them. Parsing it and writing `document.dump(2)` back destroys all
//  of that in one go - and it is worse than untidy, because `eng::Json` is
//  `nlohmann::json`, whose objects are std::map-backed and therefore SORTED.
//  The first use of the Game view's dropdown would alphabetise every section
//  and every binding in the project, so the diff would be four hundred moved
//  lines with the one changed value hidden somewhere inside it. Nobody reviews
//  that diff; they assume the editor corrupted the file.
//
//  So the document is parsed only to CHECK the file - if it will not parse,
//  nothing is written at all - and the change itself is spliced into the
//  original text, leaving every byte this function did not mean to touch
//  exactly where it was.
//
//  THIS IS THE HOUSE DECISION, not a local preference: Project.cpp already
//  writes a new project's config/engine.json as literal text for the same
//  reason, saying "a file explaining itself is worth more than one that
//  round-trips perfectly". This is that same decision from the other side.
//
//  AND IT IS CHECKED. The spliced text is parsed again and the value read
//  back before anything is written, so a scanner that lost its way cannot
//  leave a broken settings file behind - it falls back to the whole-document
//  write and says so.
// ============================================================================

// ---- a very small scanner over JSON text -----------------------------------
//
// Enough to find a named member of an object and say where its value begins
// and ends. It is NOT a parser and does not have to be: nlohmann has already
// confirmed the text is valid JSON before any of this runs, which is what
// makes a scanner this short safe.

size_t SkipBlanks(const std::string& text, size_t i) {
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' ||
                               text[i] == '\r')) {
        ++i;
    }
    return i;
}

// `i` is the opening quote. Returns one past the closing quote.
size_t PastString(const std::string& text, size_t i) {
    ++i;
    while (i < text.size()) {
        if (text[i] == '\\') {
            i += 2;   // an escape and whatever it escapes, including \" itself
            continue;
        }
        if (text[i] == '"') {
            return i + 1;
        }
        ++i;
    }
    return std::string::npos;
}

// `i` is the first character of a value. Returns one past its last.
size_t PastValue(const std::string& text, size_t i) {
    if (i >= text.size()) {
        return std::string::npos;
    }
    if (text[i] == '"') {
        return PastString(text, i);
    }
    if (text[i] == '{' || text[i] == '[') {
        int depth = 0;
        while (i < text.size()) {
            const char c = text[i];
            if (c == '"') {
                i = PastString(text, i);
                if (i == std::string::npos) {
                    return i;
                }
                continue;   // a brace inside a string is not a brace
            }
            if (c == '{' || c == '[') {
                ++depth;
            } else if (c == '}' || c == ']') {
                --depth;
                if (depth == 0) {
                    return i + 1;
                }
            }
            ++i;
        }
        return std::string::npos;
    }

    // A number, true, false or null ends where the next structural character
    // or piece of whitespace begins. None of them can contain one.
    while (i < text.size() && text[i] != ',' && text[i] != '}' && text[i] != ']' &&
           text[i] != ' ' && text[i] != '\t' && text[i] != '\n' && text[i] != '\r') {
        ++i;
    }
    return i;
}

struct Member {
    size_t keyStart   = std::string::npos;   // the opening quote of the name
    size_t valueStart = std::string::npos;
    size_t valueEnd   = std::string::npos;   // one past the last character
};

// Walks the direct members of the object whose '{' is at objectStart. Fills
// outMatch with the one named `key` if it is there, and outLast with the last
// one regardless - which is where a new member goes. Returns false if the text
// did not look the way it must.
bool ScanObject(const std::string& text, size_t objectStart, std::string_view key,
                Member& outMatch, Member& outLast) {
    if (objectStart >= text.size() || text[objectStart] != '{') {
        return false;
    }

    size_t i = SkipBlanks(text, objectStart + 1);
    while (i < text.size() && text[i] != '}') {
        if (text[i] != '"') {
            return false;
        }

        Member       m;
        m.keyStart            = i;
        const size_t nameEnd  = PastString(text, i);
        if (nameEnd == std::string::npos) {
            return false;
        }
        const std::string_view name(text.data() + i + 1, nameEnd - i - 2);

        i = SkipBlanks(text, nameEnd);
        if (i >= text.size() || text[i] != ':') {
            return false;
        }
        i            = SkipBlanks(text, i + 1);
        m.valueStart = i;
        m.valueEnd   = PastValue(text, i);
        if (m.valueEnd == std::string::npos) {
            return false;
        }

        outLast = m;
        if (!key.empty() && name == key) {
            outMatch = m;
        }

        i = SkipBlanks(text, m.valueEnd);
        if (i < text.size() && text[i] == ',') {
            i = SkipBlanks(text, i + 1);
            continue;
        }
        break;
    }
    return i < text.size() && text[i] == '}';
}

// The whitespace at the start of the line `pos` is on, so an inserted member
// lines up with the ones already there. Two spaces if `pos` is not the first
// thing on its line, which is the only sensible guess for a file written all
// on one line.
std::string IndentOf(const std::string& text, size_t pos) {
    size_t lineStart = (pos == 0) ? std::string::npos : text.rfind('\n', pos - 1);
    lineStart        = (lineStart == std::string::npos) ? 0 : lineStart + 1;

    size_t i = lineStart;
    while (i < pos && (text[i] == ' ' || text[i] == '\t')) {
        ++i;
    }
    return (i == pos) ? text.substr(lineStart, pos - lineStart) : std::string("  ");
}

// Whatever the file already uses. Mixing the two in one file is the kind of
// thing that shows up as a whole-file diff on somebody else's machine.
std::string NewlineOf(const std::string& text) {
    return text.find("\r\n") == std::string::npos ? "\n" : "\r\n";
}

// Adds `member` - already of the form "key": value - to the object whose '{'
// is at objectStart: after the last member it has, or between the braces when
// it has none.
bool AddMember(std::string& text, size_t objectStart, const std::string& member,
               bool blankLineBefore) {
    Member match;
    Member last;
    if (!ScanObject(text, objectStart, {}, match, last)) {
        return false;
    }

    const std::string nl = NewlineOf(text);

    if (last.keyStart == std::string::npos) {
        const size_t end = PastValue(text, objectStart);
        if (end == std::string::npos) {
            return false;
        }
        const std::string outer = IndentOf(text, objectStart);
        text.replace(objectStart, end - objectStart,
                     "{" + nl + outer + "  " + member + nl + outer + "}");
        return true;
    }

    const std::string inner = IndentOf(text, last.keyStart);
    text.insert(last.valueEnd, "," + (blankLineBefore ? nl : "") + nl + inner + member);
    return true;
}

// Sets section.key to `literal` in `text`, adding the key - or the whole
// section, with `note` against it - when it is not there. False means the text
// could not be navigated, which puts the caller on the fallback path.
bool SpliceSetting(std::string& text, std::string_view section, std::string_view key,
                   const std::string& literal, const std::string& note) {
    const size_t rootStart = text.find('{');
    if (rootStart == std::string::npos) {
        return false;
    }

    Member sectionMember;
    Member lastRoot;
    if (!ScanObject(text, rootStart, section, sectionMember, lastRoot)) {
        return false;
    }

    // ---- the section is not there, so it arrives whole --------------------
    if (sectionMember.keyStart == std::string::npos) {
        const std::string nl    = NewlineOf(text);
        const std::string inner = (lastRoot.keyStart == std::string::npos)
                                      ? IndentOf(text, rootStart) + "  "
                                      : IndentOf(text, lastRoot.keyStart);

        // THE NOTE GOES IN WITH IT, once, when the section is created. The
        // rest of that file explains itself to whoever opens it and a section
        // the editor added should not be the one bare exception.
        std::string body = "\"" + std::string(section) + "\": {" + nl;
        if (!note.empty()) {
            body += inner + "  \"_note\": " + eng::Json(note).dump() + "," + nl;
        }
        body += inner + "  \"" + std::string(key) + "\": " + literal + nl + inner + "}";

        return AddMember(text, rootStart, body, /*blankLineBefore=*/true);
    }

    if (sectionMember.valueStart == std::string::npos ||
        text[sectionMember.valueStart] != '{') {
        return false;   // the name is taken by something that is not a section
    }

    Member keyMember;
    Member lastInSection;
    if (!ScanObject(text, sectionMember.valueStart, key, keyMember, lastInSection)) {
        return false;
    }

    // ---- the ordinary case: one value, replaced where it sits ------------
    if (keyMember.valueStart != std::string::npos) {
        text.replace(keyMember.valueStart, keyMember.valueEnd - keyMember.valueStart,
                     literal);
        return true;
    }

    return AddMember(text, sectionMember.valueStart,
                     "\"" + std::string(key) + "\": " + literal,
                     /*blankLineBefore=*/false);
}

// Sets one key in one section of config/engine.json and writes the file.
//
// `literal` IS THE JSON TEXT THAT GOES IN THE FILE, not a value to be
// formatted here. That is what lets a float be written as `32.0` rather than
// as the 17 digits `double` would produce, and it costs nothing in safety:
// the text is parsed back before anything is written, and what it parses to is
// what the check below demands the file now contain. A literal that is not
// valid JSON is refused here rather than written and discovered later.
//
// A file that is MISSING is written fresh: refusing would mean a project with
// no settings could never be given any. A file that is PRESENT BUT WILL NOT
// PARSE is reported and left strictly alone, because overwriting it would
// throw away whatever is in there - which, for this file, is every key binding
// in the project.
bool SetSetting(std::string_view section, std::string_view key,
                const std::string& literal, std::string& outError) {
    const std::string sectionName(section);
    const std::string keyName(key);
    const std::string note = NoteForSection(section);

    const eng::Json value = eng::Json::parse(literal, nullptr, false);
    if (value.is_discarded()) {
        outError = "'" + literal + "' is not a value this file can hold";
        return false;
    }

    std::string text;
    std::string readError;
    eng::Json   document = eng::Json::object();

    if (eng::FileSystem::ReadTextFile(kConfigPath, text, readError)) {
        document = eng::Json::parse(text, nullptr, false);
        if (document.is_discarded()) {
            outError = std::string(kConfigPath) +
                       " could not be read as JSON, so it has been left alone. Fix "
                       "the file and try again - overwriting it would throw away "
                       "whatever else is in there";
            return false;
        }
        if (!document.is_object()) {
            outError = std::string(kConfigPath) + " is not a set of settings";
            return false;
        }
    } else if (eng::FileSystem::Exists(kConfigPath)) {
        // ==================================================================
        //  THERE AND UNREADABLE IS NOT THE SAME AS NOT THERE.
        //
        //  ReadTextFile answers false for both, and taking the fresh-file
        //  path on a locked or permission-denied file would replace every
        //  setting in the project - every key binding included - with a
        //  file holding one key. The one case where that is the right
        //  answer is the file genuinely not existing, so that is the case
        //  that is asked about rather than assumed.
        // ==================================================================
        outError = std::string(kConfigPath) +
                   " is there but could not be read (" + readError +
                   "), so it has been left alone";
        return false;
    } else {
        text = "{\n}\n";
    }

    // ---- the splice, proved before it is written -------------------------
    std::string spliced = text;
    if (SpliceSetting(spliced, section, keyName, literal, note)) {
        const eng::Json after = eng::Json::parse(spliced, nullptr, false);
        if (!after.is_discarded() && after.is_object() && after.contains(sectionName) &&
            after[sectionName].is_object() && after[sectionName].contains(keyName) &&
            after[sectionName][keyName] == value) {
            return eng::FileSystem::WriteTextFile(kConfigPath, spliced, outError);
        }
    }

    // ---- and the fallback, for a file shaped in a way the scanner cannot
    //      follow. The layout and the order of the keys do not survive it,
    //      which is why it is said out loud rather than swallowed.
    bool replacedSection = false;
    if (!document.contains(sectionName) || !document[sectionName].is_object()) {
        // A NAME HELD BY SOMETHING THAT IS NOT A SECTION is the one value
        // this loses - `"presentation": 5` becomes `"presentation": { ... }`,
        // because there is nowhere in a set of settings to put the 5. Said
        // separately below rather than folded into "every setting is still
        // there", which would not have been true.
        replacedSection = document.contains(sectionName);
        document[sectionName] = eng::Json::object();
        if (!note.empty()) {
            document[sectionName]["_note"] = note;
        }
    }
    document[sectionName][keyName] = value;

    ENGINE_LOG_WARN(eng::Channels::kEditor,
                    "{} could not be edited in place, so the whole file has been "
                    "rewritten - the settings are still there, but the layout and "
                    "the order of the keys have changed",
                    kConfigPath);
    if (replacedSection) {
        ENGINE_LOG_WARN(eng::Channels::kEditor,
                        "and \"{}\" was not a set of settings, so whatever it held "
                        "has been replaced by one",
                        sectionName);
    }
    return eng::FileSystem::WriteTextFile(kConfigPath, document.dump(2) + "\n", outError);
}

// ============================================================================
//  WRITE, THEN MAKE IT TRUE OF THE RUNNING ENGINE.
//
//  Every public writer ends here. The engine re-reads the file it read at
//  start-up and applies whatever can be applied, so a change is live before
//  the widget that made it has finished drawing - which is the whole point of
//  editing settings in an editor rather than in a text editor.
//
//  THE WRITE IS NOT UNDONE WHEN THE RELOAD FAILS, and that is deliberate. The
//  file is the project's answer; the engine's copy is a consequence of it. A
//  reload that refuses has already said why in the log, and rewinding the file
//  would throw away what somebody just asked for in order to protect a cache.
// ============================================================================
bool WriteAndApply(std::string_view section, std::string_view key,
                   const std::string& literal, std::string& outError) {
    if (!SetSetting(section, key, literal, outError)) {
        return false;
    }

    std::string reloadError;
    if (!eng::Engine::Get().ReloadSettings(reloadError)) {
        outError = "the setting was written, but the file could not be read back: " +
                   reloadError;
        return false;
    }

    ENGINE_LOG_INFO(eng::Channels::kEditor, "{}.{} set to {} in {}", section, key,
                    literal, kConfigPath);
    return true;
}

// The shortest text that reads back as the SAME float.
//
// Not value.dump(). nlohmann holds a number as a double, so a float arrives
// widened - -9.81f becomes -9.8100004196166992 - and a settings file full of
// those is one nobody wants to open. A whole number keeps a ".0" so that the
// file goes on saying "this is a measurement", which is how the hand-written
// sections already spell gravity.
//
// EMPTY MEANS REFUSE. An infinity or a NaN has no JSON spelling at all, and
// quietly writing some other number instead would tell the caller the value
// it asked for is in the file when a value it never asked for is - which, for
// units.pixelsPerUnit, is the difference between a 32-pixel world unit and a
// 1-pixel one.
std::string NumberLiteral(float value) {
    if (!std::isfinite(value)) {
        return {};
    }
    if (value == std::floor(value) && std::fabs(value) < 1.0e7f) {
        return std::format("{:.1f}", value);
    }
    return std::format("{}", value);
}

} // namespace

// ---------------------------------------------------------------------------
//  Reading
// ---------------------------------------------------------------------------

std::string PresentationMode() {
    return eng::Engine::Get().Config().presentation;
}

bool WindowResizable() {
    return eng::Engine::Get().Config().windowResizable;
}

bool HasSetting(std::string_view section, std::string_view key) {
    // The FILE, not the engine's parsed copy: the whole question is whether
    // somebody wrote this down, and BootConfig cannot tell a value that was
    // written from a default that was assumed.
    std::string text;
    std::string readError;
    if (!eng::FileSystem::ReadTextFile(kConfigPath, text, readError)) {
        return false;
    }

    const eng::Json document = eng::Json::parse(text, nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        return false;
    }

    const std::string sectionName(section);
    const std::string keyName(key);
    return document.contains(sectionName) && document[sectionName].is_object() &&
           document[sectionName].contains(keyName);
}

// ---------------------------------------------------------------------------
//  Writing
// ---------------------------------------------------------------------------

bool SetString(std::string_view section, std::string_view key,
               std::string_view value, std::string& outError) {
    // Through eng::Json so that a quote, a backslash or a non-ASCII character
    // in the value is escaped the way the file's own parser expects.
    //
    // AND dump() IS THE ONE THING IN THIS FILE THAT THROWS. It refuses text
    // that is not valid UTF-8, which is reachable: a path typed with a
    // non-ASCII character arrives through MSVC's narrow conversion as bytes
    // in the system code page, not as UTF-8. Everything else here is
    // exception-free by construction, so the exception is turned into the
    // refusal the panel already knows how to show.
    std::string literal;
    try {
        literal = eng::Json(std::string(value)).dump();
    } catch (const std::exception& e) {
        outError = std::string("that text cannot be written to a settings file (") +
                   e.what() + ")";
        return false;
    }
    return WriteAndApply(section, key, literal, outError);
}

bool SetBool(std::string_view section, std::string_view key, bool value,
             std::string& outError) {
    return WriteAndApply(section, key, value ? "true" : "false", outError);
}

bool SetInt(std::string_view section, std::string_view key, int value,
            std::string& outError) {
    return WriteAndApply(section, key, std::to_string(value), outError);
}

bool SetNumber(std::string_view section, std::string_view key, float value,
               std::string& outError) {
    const std::string literal = NumberLiteral(value);
    if (literal.empty()) {
        outError = std::string(key) + " must be an ordinary number";
        return false;
    }
    return WriteAndApply(section, key, literal, outError);
}

bool SetNumbers(std::string_view section, std::string_view key, const float* values,
                std::size_t count, std::string& outError) {
    if (values == nullptr || count == 0) {
        outError = "nothing to write";
        return false;
    }

    // Written on one line, the way a pair of coordinates is written by hand -
    // nlohmann's dump would put each number on a line of its own.
    std::string literal = "[";
    for (std::size_t i = 0; i < count; ++i) {
        const std::string number = NumberLiteral(values[i]);
        if (number.empty()) {
            outError = std::string(key) + " must be ordinary numbers";
            return false;
        }
        if (i > 0) {
            literal += ", ";
        }
        literal += number;
    }
    literal += "]";

    return WriteAndApply(section, key, literal, outError);
}

bool SetPresentationMode(const std::string& spec, std::string& outError) {
    // ======================================================================
    //  CHECKED BEFORE IT IS WRITTEN, by the engine's own reader.
    //
    //  One parser decides what "16:9" means - see Presentation::ParseSpec -
    //  so a value this function accepted but the engine then refused at
    //  start-up would be a setting that looked applied in the editor and did
    //  nothing in the game. Which is the exact class of bug this whole file
    //  exists to close.
    //
    //  Nothing applies the parsed result here. Engine::ReloadSettings reads
    //  the file back and applies it through the same code start-up uses, so
    //  the shape the editor shows is the shape the engine decided on, not a
    //  second opinion that could drift from it.
    // ======================================================================
    eng::Presentation::Mode mode = eng::Presentation::Mode::Free;
    int                     x    = 0;
    int                     y    = 0;
    if (!eng::Presentation::ParseSpec(spec, mode, x, y, outError)) {
        return false;
    }

    return SetString("presentation", "mode", spec, outError);
}

bool SetWindowResizable(bool on, std::string& outError) {
    // THE EDITOR'S OWN WINDOW IS NOT TOUCHED, which is the whole reason
    // Engine::Options::gameOwnsTheWindow exists: this is a statement about
    // the shipped game, and locking the editor because a project said so
    // would leave no way back except hand-editing this file.
    return SetBool("window", "resizable", on, outError);
}

} // namespace editor
