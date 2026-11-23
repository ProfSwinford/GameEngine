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

#include <cstddef>
#include <string>
#include <string_view>

namespace editor {
namespace {

constexpr const char* kConfigPath = "config/engine.json";

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
// A file that is MISSING is written fresh: refusing would mean a project with
// no settings could never be given any. A file that is PRESENT BUT WILL NOT
// PARSE is reported and left strictly alone, because overwriting it would
// throw away whatever is in there - which, for this file, is every key binding
// in the project.
bool SetSetting(std::string_view section, std::string_view key, const eng::Json& value,
                const std::string& note, std::string& outError) {
    const std::string sectionName(section);
    const std::string keyName(key);

    std::string text;
    std::string readError;
    const bool  haveFile = eng::FileSystem::ReadTextFile(kConfigPath, text, readError);

    eng::Json document = eng::Json::object();
    if (haveFile) {
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
    } else {
        text = "{\n}\n";
    }

    // ---- the splice, proved before it is written -------------------------
    std::string spliced = text;
    if (SpliceSetting(spliced, section, keyName, value.dump(), note)) {
        const eng::Json after = eng::Json::parse(spliced, nullptr, false);
        if (!after.is_discarded() && after.is_object() && after.contains(sectionName) &&
            after[sectionName].is_object() && after[sectionName].contains(keyName) &&
            after[sectionName][keyName] == value) {
            return eng::FileSystem::WriteTextFile(kConfigPath, spliced, outError);
        }
    }

    // ---- and the fallback, for a file shaped in a way the scanner cannot
    //      follow. Every value survives; the layout and the order of the keys
    //      do not, which is why it is said out loud rather than swallowed.
    if (!document.contains(sectionName) || !document[sectionName].is_object()) {
        document[sectionName] = eng::Json::object();
        if (!note.empty()) {
            document[sectionName]["_note"] = note;
        }
    }
    document[sectionName][keyName] = value;

    ENGINE_LOG_WARN(eng::Channels::kEditor,
                    "{} could not be edited in place, so the whole file has been "
                    "rewritten - every setting is still there, but the layout and "
                    "the order of the keys have changed",
                    kConfigPath);
    return eng::FileSystem::WriteTextFile(kConfigPath, document.dump(2) + "\n", outError);
}

} // namespace

std::string PresentationMode() {
    return eng::Engine::Get().Config().presentation;
}

bool WindowResizable() {
    return eng::Engine::Get().Config().windowResizable;
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
    // ======================================================================
    eng::Presentation::Mode mode = eng::Presentation::Mode::Free;
    int                     x    = 0;
    int                     y    = 0;
    if (!eng::Presentation::ParseSpec(spec, mode, x, y, outError)) {
        return false;
    }

    if (!SetSetting("presentation", "mode", eng::Json(spec),
                    "What shape the finished game is. \"free\" lets it fill the "
                    "window, so a wider monitor sees MORE OF THE LEVEL. A ratio like "
                    "\"16:9\" pins the shape; a size like \"1920x1080\" pins the exact "
                    "pixels. Whatever space is left over is filled with BLACK. Set "
                    "from the Game view's dropdown in the editor.",
                    outError)) {
        return false;
    }

    // ---- and the engine running right now --------------------------------
    //
    // The settings file is read once at start-up, so without this the Game
    // view would go on showing the old shape until the editor was restarted -
    // and a setting you cannot see take effect is a setting you do not trust.
    switch (mode) {
        case eng::Presentation::Mode::Aspect:     eng::Presentation::SetAspect(x, y); break;
        case eng::Presentation::Mode::Resolution: eng::Presentation::SetResolution(x, y); break;
        case eng::Presentation::Mode::Free:       eng::Presentation::SetFree(); break;
    }
    eng::Engine::Get().SetPresentationSetting(spec);

    ENGINE_LOG_INFO(eng::Channels::kEditor,
                    "the game will be presented as '{}' - written to {}", spec,
                    kConfigPath);
    return true;
}

bool SetWindowResizable(bool on, std::string& outError) {
    if (!SetSetting("window", "resizable", eng::Json(on),
                    "false locks the BUILT GAME's window to the width and height "
                    "above - the editor's own window is never affected. Set it from "
                    "the Resizable box in the Game view.",
                    outError)) {
        return false;
    }

    // THE EDITOR'S OWN WINDOW IS NOT TOUCHED, which is the whole reason
    // Engine::Options::gameOwnsTheWindow exists: this is a statement about
    // the shipped game, and locking the editor because a project said so
    // would leave no way back except hand-editing this file.
    eng::Engine::Get().SetWindowResizableSetting(on);

    ENGINE_LOG_INFO(eng::Channels::kEditor,
                    "the game's window will be {} - written to {}",
                    on ? "resizable" : "a fixed size", kConfigPath);
    return true;
}

} // namespace editor
