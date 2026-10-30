// ============================================================================
//  ScriptAbi.cpp - see ScriptAbi.h for what this prevents and why the engine
//  owns it rather than the editor.
// ============================================================================

#include <engine/project/ScriptAbi.h>

#include <engine/fs/FileSystem.h>
#include <engine/project/EngineBuild.h>

#include <map>
#include <sstream>
#include <string>

namespace eng {
namespace {

// ---------------------------------------------------------------------------
//  WHICH C RUNTIME THIS ENGINE WAS BUILT WITH.
//
//  Compiled into the engine's own translation unit, so it reports the
//  engine's configuration rather than that of whoever is asking. The editor
//  has a define of its own for the same thing and uses it to pass /MDd or /MD
//  to the compiler; both come from $<CONFIG> on targets that are always built
//  together, so they cannot disagree.
//
//  Defaulted rather than required, so that a build which has not been told
//  still compiles. It then reports the same value every time, which makes
//  every comparison a match - the pre-existing behaviour, which is the right
//  thing for a check to degrade into.
// ---------------------------------------------------------------------------
#if !defined(ENGINE_IS_DEBUG)
#define ENGINE_IS_DEBUG 0
#endif

constexpr const char* kCrt = ENGINE_IS_DEBUG ? "/MDd" : "/MD";

// Splits `key=value` lines into a map, ignoring blank lines and anything
// without an `=`. Deliberately forgiving: this file is read far more often
// than it is written, and a stamp that fails to parse would be reported as a
// mismatch, which is the one wrong answer that stops somebody working.
std::map<std::string, std::string> Parse(std::string_view text) {
    std::map<std::string, std::string> out;

    // BRACES, not parentheses. `std::istringstream lines(std::string(text));`
    // declares a FUNCTION called `lines` taking a std::string - the most
    // vexing parse - and the only symptom is getline failing to find an
    // overload, which reads as a missing include.
    std::istringstream lines{std::string(text)};
    std::string        line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();   // a stamp written on Windows, read anywhere
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos || equals == 0) {
            continue;
        }
        out[line.substr(0, equals)] = line.substr(equals + 1);
    }
    return out;
}

// How to describe a build, for a message somebody has to act on.
std::string Describe(const std::map<std::string, std::string>& stamp) {
    const auto engine = stamp.find("engine");
    const auto crt    = stamp.find("crt");

    std::string described;
    if (engine != stamp.end() && !engine->second.empty()) {
        described = engine->second;
    }
    if (crt != stamp.end() && !crt->second.empty()) {
        described += described.empty() ? crt->second : " (" + crt->second + ")";
    }
    return described.empty() ? "an unidentified build" : described;
}

} // namespace

std::string ScriptAbi::StampVirtualPath() {
    return ".build/buildconfig.txt";
}

std::string ScriptAbi::Stamp() {
    const EngineBuildInfo& engine = EngineBuild::Running();

    // SORTED BY KEY, because this text is compared against what a previous
    // build wrote and two orderings of the same facts must not read as a
    // difference. A std::map does it for free in Parse; writing them in order
    // here keeps the file readable by a person as well.
    std::ostringstream stamp;
    stamp << "abi=" << engine.scriptAbi << "\n";
    stamp << "crt=" << kCrt << "\n";
    stamp << "engine=" << engine.id << "\n";
    return stamp.str();
}

ScriptAbiCheck ScriptAbi::Check(std::string_view recorded) {
    ScriptAbiCheck result;

    const std::map<std::string, std::string> mine  = Parse(Stamp());
    const std::map<std::string, std::string> their = Parse(recorded);

    if (their.empty()) {
        // Nothing recorded at all. An exported game, or a project nothing has
        // built yet. Silent - see the header.
        result.verdict = ScriptAbiCheck::Verdict::Unverifiable;
        return result;
    }

    // ======================================================================
    //  A DISAGREEMENT BEATS A MISSING KEY, AND THE ORDER IS THE WHOLE POINT.
    //
    //  This loop used to return Unverifiable the moment it met a key the
    //  recorded stamp did not have, without looking at the keys it DID have.
    //  std::map visits in sorted order - abi, crt, engine - so `abi` was
    //  always examined first, and a stamp without it never had its `crt`
    //  compared at all.
    //
    //  That is not a corner case. It is EXACTLY the stamp every project in
    //  existence had before this check was added, because the previous
    //  version of the editor wrote only `crt=` and `enginelib=`. So the one
    //  stamp format already on disk everywhere was precisely the one that
    //  slipped through: pull the change, build the other configuration, run
    //  the player, and it reported "no way to tell which engine built them"
    //  about a stamp that said so plainly - then loaded the library and
    //  corrupted the heap. The branch written to be kind to people upgrading
    //  into this was the branch that hurt them.
    //
    //  So: compare everything comparable FIRST. Unverifiable is the weakest
    //  answer and is only reached when nothing that could be compared
    //  disagreed.
    // ======================================================================
    std::string absentKey;

    for (const auto& [key, value] : mine) {
        const auto found = their.find(key);
        if (found == their.end()) {
            // Remembered, not acted on. See above.
            if (absentKey.empty()) {
                absentKey = key;
            }
            continue;
        }

        if (found->second != value) {
            result.verdict = ScriptAbiCheck::Verdict::Mismatch;

            // NAMES BOTH SIDES, THE FIELD THAT DIFFERS, AND THE WAY OUT.
            //
            // Describe() reports the engine id and the C runtime, which are
            // what a person recognises - but when the SCRIPT ABI is the thing
            // that changed, those two are identical on both sides and the
            // message would read "built by 1.0.0 Debug, but this is 1.0.0
            // Debug". That is worse than terse: it reads as a bug in the
            // engine rather than as a stale build, and the ABI number is
            // bumped independently of the version precisely so this case
            // happens without the version moving. See EngineBuild.h.
            const std::string difference =
                Describe(their) == Describe(mine)
                    ? " - they differ in '" + key + "' (" + found->second +
                          ", not " + value + ")"
                    : std::string{};

            result.explanation =
                "the compiled scripts were built by " + Describe(their) +
                ", but this is " + Describe(mine) + difference +
                ". Loading them would corrupt memory rather than fail, so they have "
                "not been loaded. Run 'editor --build-scripts' to rebuild them for "
                "this engine.";
            return result;
        }
    }

    if (!absentKey.empty()) {
        // Everything that could be compared agreed, and something could not
        // be compared. Reported as unverifiable rather than as a mismatch so
        // that an older .build/ folder keeps working exactly as it did,
        // instead of refusing until somebody rebuilds for a reason they
        // cannot see.
        result.verdict     = ScriptAbiCheck::Verdict::Unverifiable;
        result.explanation =
            "the compiled scripts do not record '" + absentKey +
            "', so there is no way to be sure which engine built them. Everything "
            "they do record matches this one. They were built before this check "
            "existed; the next build will record it.";
        return result;
    }

    result.verdict = ScriptAbiCheck::Verdict::Match;
    return result;
}

ScriptAbiCheck ScriptAbi::CheckRecorded() {
    std::string text;
    std::string error;

    // A missing file is the ordinary case for a shipped game, so the error is
    // discarded rather than reported: Check treats empty text as
    // unverifiable, which is exactly right.
    FileSystem::ReadTextFile(StampVirtualPath(), text, error);
    return Check(text);
}

} // namespace eng
