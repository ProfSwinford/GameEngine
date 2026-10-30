// ============================================================================
//  Tests for eng::ScriptAbi - "were these compiled scripts built for THIS
//  engine?"
//
//  WHY THIS IS WORTH TESTING MORE THAN MOST THINGS
//
//  Both wrong answers are expensive, and they are expensive in opposite
//  directions.
//
//  A FALSE MATCH loads a library built against a different C runtime. That
//  does not fail where you can see it: the DLL loads, every symbol resolves,
//  the log says it worked, and the program dies later on an allocation that
//  crosses the heap boundary. This was reproduced in this project as an
//  access violation with no output at all.
//
//  A FALSE MISMATCH refuses to run somebody's scripts for a reason that is
//  not true. In the editor that is survivable - it rebuilds - but in the
//  player it means a game that silently does nothing.
//
//  So the interesting cases are the boring-looking ones: an empty stamp, a
//  stamp with extra keys in it, a stamp from an older build. Each has a
//  different right answer and none of them is "mismatch".
// ============================================================================

#include <doctest/doctest.h>

#include <engine/project/ScriptAbi.h>

#include <string>

using namespace eng;

namespace {
// Replaces one whole `key=...` line, newline intact. Doing this with a
// fixed-width replace eats the newline and silently tests a MALFORMED stamp
// instead of a different one - which still passes, for the wrong reason.
std::string WithLine(std::string stamp, std::string_view key, std::string_view value) {
    const std::size_t at = stamp.find(std::string(key) + "=");
    REQUIRE(at != std::string::npos);
    const std::size_t end = stamp.find('\n', at);
    REQUIRE(end != std::string::npos);
    stamp.replace(at, end - at, std::string(key) + "=" + std::string(value));
    return stamp;
}
} // namespace

TEST_CASE("a stamp this engine wrote itself matches") {
    // The round trip. If this ever fails, every build in the project starts
    // recompiling its scripts on every run.
    const ScriptAbiCheck check = ScriptAbi::Check(ScriptAbi::Stamp());

    CHECK(check.verdict == ScriptAbiCheck::Verdict::Match);
    CHECK(check.explanation.empty());
}

TEST_CASE("the stamp names the three things that decide loadability") {
    const std::string stamp = ScriptAbi::Stamp();

    // Pinned by name because the EDITOR writes this same file and adds a line
    // of its own, and because the player reads it. A key quietly renamed on
    // one side would make every comparison unverifiable rather than wrong,
    // which is the sort of failure that hides.
    CHECK(stamp.find("abi=") != std::string::npos);
    CHECK(stamp.find("crt=") != std::string::npos);
    CHECK(stamp.find("engine=") != std::string::npos);

    // The C runtime is reported as the flag a compiler would be given, so the
    // message reads the way the build does.
    CHECK((stamp.find("crt=/MD\n") != std::string::npos ||
           stamp.find("crt=/MDd\n") != std::string::npos));
}

// ===========================================================================
//  Silence, which is the answer for a shipped game
// ===========================================================================
TEST_CASE("nothing recorded is unverifiable and says nothing") {
    // AN EXPORTED GAME ALWAYS LANDS HERE. It ships one userContent.dll built
    // beside the engine.dll that went with it, so they match by construction
    // and there is no build folder to record anything in. An explanation here
    // would print a warning in every shipped game.
    for (const char* nothing : {"", "   ", "\n\n", "# just a comment\n"}) {
        const ScriptAbiCheck check = ScriptAbi::Check(nothing);
        CHECK(check.verdict == ScriptAbiCheck::Verdict::Unverifiable);
        CHECK(check.explanation.empty());
    }
}

// ===========================================================================
//  Keys this engine does not own
// ===========================================================================
TEST_CASE("keys the engine does not know about are ignored") {
    // THIS IS WHAT LETS TWO LAYERS SHARE ONE FILE. The editor records
    // `enginelib`, which the engine cannot check - it has no way to know
    // which spelling of its own location a build linked against. Treating an
    // unknown key as a difference would make every editor-written stamp a
    // mismatch, which is to say it would refuse to load every script in the
    // project.
    const std::string withEditorsOwnLine =
        ScriptAbi::Stamp() + "enginelib=C:/somewhere/engine.lib\n" +
        "somethingFromTheFuture=42\n";

    const ScriptAbiCheck check = ScriptAbi::Check(withEditorsOwnLine);
    CHECK(check.verdict == ScriptAbiCheck::Verdict::Match);
}

TEST_CASE("a missing key is unverifiable only when everything else agrees") {
    // A stamp written before that key existed, with the keys it DOES have
    // matching. Somebody mid-upgrade whose .build/ predates the check - the
    // honest answer is "cannot be sure", because refusing would stop them
    // working for a reason they cannot see and have not caused.
    //
    // THE crt MUST MATCH THIS ENGINE for that to be the right answer, which
    // is why it is taken from Stamp() rather than written as a literal. An
    // earlier version of this test hard-coded "/MDd" and so, in a Release
    // build, asserted that a stamp with a DISAGREEING crt was merely
    // unverifiable - pinning the bug below instead of catching it.
    const std::string crtLine =
        ScriptAbi::Stamp().substr(ScriptAbi::Stamp().find("crt="));
    const std::string justCrt = crtLine.substr(0, crtLine.find('\n') + 1);

    const ScriptAbiCheck check = ScriptAbi::Check(justCrt + "enginelib=x\n");

    CHECK(check.verdict == ScriptAbiCheck::Verdict::Unverifiable);

    // WITH an explanation this time, unlike the empty case: something WAS
    // recorded, so there is a reason worth one line.
    CHECK_FALSE(check.explanation.empty());
}

TEST_CASE("a missing key does not excuse a key that is present and disagrees") {
    // ======================================================================
    //  THE BUG THIS EXISTS TO CATCH, and it was a real one.
    //
    //  Check walks the keys in sorted order - abi, crt, engine - and used to
    //  return Unverifiable the moment it met a key the stamp did not have,
    //  without looking at the keys it DID have. 'abi' sorts first, so a stamp
    //  without it never had its 'crt' compared.
    //
    //  That is exactly the stamp every project already had: the previous
    //  editor wrote only 'crt=' and 'enginelib='. So the single stamp format
    //  in existence was the one that slipped through - pull the change, build
    //  the other configuration, run the player, and a stamp that said plainly
    //  which runtime it was built with was reported as "no way to tell", and
    //  the library was loaded anyway.
    // ======================================================================
    const bool iAmDebug = ScriptAbi::Stamp().find("crt=/MDd") != std::string::npos;
    const std::string theOtherCrt = iAmDebug ? "crt=/MD\n" : "crt=/MDd\n";

    // No abi, no engine - just the two keys the old editor recorded.
    const ScriptAbiCheck check = ScriptAbi::Check(theOtherCrt + "enginelib=x\n");

    REQUIRE(check.verdict == ScriptAbiCheck::Verdict::Mismatch);
    CHECK(check.explanation.find("not been loaded") != std::string::npos);
}

// ===========================================================================
//  The disagreements, which are the whole point
// ===========================================================================
TEST_CASE("a different C runtime is a mismatch") {
    // The one somebody hits by accident: the Release editor built the
    // scripts, the Debug player is running them.
    const std::string theirs = "abi=1\ncrt=/MDd\nengine=1.0.0 Debug\n";
    const std::string other  = "abi=1\ncrt=/MD\nengine=1.0.0 Release\n";

    // Whichever of the two this engine is, the OTHER one must be refused.
    const std::string notMine =
        (ScriptAbi::Stamp().find("crt=/MDd") != std::string::npos) ? other : theirs;

    const ScriptAbiCheck check = ScriptAbi::Check(notMine);
    REQUIRE(check.verdict == ScriptAbiCheck::Verdict::Mismatch);

    // THE MESSAGE HAS TO BE ACTIONABLE. "ABI mismatch" sends somebody reading
    // source; naming both builds and the command sends them to the fix.
    CHECK(check.explanation.find("built by") != std::string::npos);
    CHECK(check.explanation.find("--build-scripts") != std::string::npos);
    CHECK(check.explanation.find("not been loaded") != std::string::npos);
}

TEST_CASE("a different script ABI is a mismatch, in both directions") {
    // THE CASE THE TIMESTAMPS CANNOT SEE. An engine installed over the top of
    // an old one leaves every object looking current while having been built
    // against different headers. And because this is a comparison rather than
    // a date, going BACK to an older engine is caught too - which no
    // one-directional staleness test in the script build would notice.
    const std::string newer = WithLine(ScriptAbi::Stamp(), "abi", "99");
    const std::string older = WithLine(ScriptAbi::Stamp(), "abi", "0");

    CHECK(ScriptAbi::Check(newer).verdict == ScriptAbiCheck::Verdict::Mismatch);
    CHECK(ScriptAbi::Check(older).verdict == ScriptAbiCheck::Verdict::Mismatch);
}

TEST_CASE("a different engine build is a mismatch") {
    const std::string theirs = WithLine(ScriptAbi::Stamp(), "engine", "0.9.0 Debug");

    const ScriptAbiCheck check = ScriptAbi::Check(theirs);
    CHECK(check.verdict == ScriptAbiCheck::Verdict::Mismatch);
    CHECK(check.explanation.find("0.9.0") != std::string::npos);
}

TEST_CASE("an ABI-only difference does not report two identical builds") {
    // The engine id and the C runtime are what a person recognises, so the
    // message leads with them - but when the SCRIPT ABI is what changed those
    // two are identical on both sides, and the message read "built by 1.0.0
    // Debug, but this is 1.0.0 Debug". That is worse than terse: it reads as a
    // bug in the engine rather than as a stale build.
    //
    // And it is the EXPECTED case, not a corner: the ABI number is bumped
    // independently of the version precisely so that an ABI change needs no
    // version bump. See EngineBuild.h.
    const ScriptAbiCheck check =
        ScriptAbi::Check(WithLine(ScriptAbi::Stamp(), "abi", "99"));

    REQUIRE(check.verdict == ScriptAbiCheck::Verdict::Mismatch);

    // It must name the field that actually differs, and both values.
    CHECK(check.explanation.find("'abi'") != std::string::npos);
    CHECK(check.explanation.find("99") != std::string::npos);
}

// ===========================================================================
//  Reading a file written somewhere else
// ===========================================================================
TEST_CASE("a stamp with Windows line endings is understood") {
    // Written on Windows, possibly read on something else - and the whole
    // file is one key per line, so a stray carriage return would attach
    // itself to every value and turn every comparison into a mismatch.
    std::string windows;
    for (char c : ScriptAbi::Stamp()) {
        if (c == '\n') {
            windows += '\r';
        }
        windows += c;
    }

    CHECK(ScriptAbi::Check(windows).verdict == ScriptAbiCheck::Verdict::Match);
}

TEST_CASE("junk in the stamp is skipped rather than failing the whole file") {
    // Forgiving on purpose. This file is read far more often than it is
    // written, and a parse failure reported as a mismatch is the one wrong
    // answer that stops somebody working.
    const std::string messy = "\n= no key\nnot a pair at all\n" + ScriptAbi::Stamp() +
                              "\n\ntrailing=rubbish\n";

    CHECK(ScriptAbi::Check(messy).verdict == ScriptAbiCheck::Verdict::Match);
}

TEST_CASE("the stamp lives where both the engine and the editor look") {
    // Named by the engine and used by the editor's BuildStampFile, so that
    // the writer and the reader cannot disagree about which file this is.
    CHECK(ScriptAbi::StampVirtualPath() == ".build/buildconfig.txt");
}
