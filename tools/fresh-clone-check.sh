#!/usr/bin/env bash
# =============================================================================
#  The fresh-clone check.
#
#  It clones the project into a temporary folder and builds it from nothing,
#  which is the only honest way to find out whether everything that matters was
#  actually committed. It catches the file you forgot to "git add", and it
#  catches it in a couple of minutes instead of in the lab.
#
#  usage:  tools/fresh-clone-check.sh <repo-url-or-path> [preset]
# =============================================================================
set -euo pipefail

REPO_URL="${1:-}"
PRESET="${2:-debug}"

if [[ -z "$REPO_URL" ]]; then
    echo "usage: $0 <repo-url-or-path> [preset]" >&2
    echo "  e.g. $0 https://github.com/ProfSwinford/GameEngine" >&2
    echo "       $0 . debug        # check the local repo's committed state" >&2
    exit 2
fi

WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

fail() {
    echo "" >&2
    echo "FRESH CLONE CHECK FAILED: $*" >&2
    exit 1
}

echo "==> Cloning into $WORKDIR"
git clone --depth 1 "$REPO_URL" "$WORKDIR/repo"

cd "$WORKDIR/repo"

# --- configure -------------------------------------------------------------
# The first configure downloads and builds SDL, ImGui, doctest and
# nlohmann/json. On a cold machine this takes several minutes and it is NOT
# hung.
echo "==> Configuring (preset: $PRESET) - the first run compiles SDL from source"
cmake --preset "$PRESET" || fail "configure failed on a fresh clone"

# --- build -----------------------------------------------------------------
echo "==> Building"
cmake --build --preset "$PRESET" || fail "build failed on a fresh clone"

# --- confirm the executables exist -----------------------------------------
# Multi-config generators (Visual Studio) put binaries in a per-config
# subdirectory; single-config ones (Ninja, Makefiles) do not. Check both rather
# than assuming, because assuming is how this script passes on one machine and
# fails on another - which is the exact failure it exists to catch.
BIN_DIR="build/$PRESET/bin"
CONFIG_DIR=""
case "$PRESET" in
    release) CONFIG_DIR="Release" ;;
    *)       CONFIG_DIR="Debug"   ;;
esac

find_binary() {
    local name="$1"
    for candidate in \
        "$BIN_DIR/$name" \
        "$BIN_DIR/$name.exe" \
        "$BIN_DIR/$CONFIG_DIR/$name" \
        "$BIN_DIR/$CONFIG_DIR/$name.exe"
    do
        if [[ -f "$candidate" ]]; then
            echo "$candidate"
            return 0
        fi
    done
    return 1
}

for target in sandbox editor tests; do
    if path="$(find_binary "$target")"; then
        echo "    found $target at $path"
    else
        fail "$target was not produced. Looked in $BIN_DIR and $BIN_DIR/$CONFIG_DIR."
    fi
done

# --- the project's scripts -------------------------------------------------
# CMake does not build these. The editor compiles them itself, and this is the
# headless way to ask it to - so this step doubles as a check that the editor
# can find a C++ compiler on this machine at all.
echo "==> Building the project's scripts (editor --build-scripts)"
editor_path="$(find_binary editor)" || fail "the editor was not produced"
"$editor_path" --build-scripts || fail "the editor could not compile the scripts under assets/"

# --- run the tests ---------------------------------------------------------
# A fresh clone that builds but does not pass is not a fresh clone that works.
echo "==> Running the test suite"
ctest --preset "$PRESET" --output-on-failure || fail "the test suite did not pass on a fresh clone"

# --- assets actually arrived -----------------------------------------------
# The classic failure: a scene that loads on the author's machine
# and not on a fresh clone, because a .bmp was never committed. Checking for the
# files is cheaper than discovering it from a magenta screen.
for asset in \
    assets/scenes/orbit_test.json \
    assets/scenes/collector.json \
    assets/textures/checker_red.bmp \
    assets/textures/checker_blue.bmp \
    assets/textures/checker_green.bmp \
    assets/textures/marker_up.bmp \
    assets/textures/missing.bmp \
    assets/scripts/Orbiter.cpp \
    config/engine.json
do
    [[ -f "$asset" ]] || fail "$asset is missing from the clone - was it committed? Check .gitignore."
done
echo "    all assets and the config file are present"

# --- the check that proves .gitignore is complete --------------------------
# The part worth relying on before handing work in.
# A clean build must leave the working tree clean. If it does not, the build is
# writing something that is tracked, or .gitignore has a hole - and either way
# the next person to clone gets a spurious diff.
echo "==> Checking that a full build left the tree clean"
STATUS="$(git status --porcelain)"
if [[ -n "$STATUS" ]]; then
    echo "" >&2
    echo "git status --porcelain printed:" >&2
    echo "$STATUS" >&2
    fail ".gitignore is incomplete - a full build dirtied the working tree."
fi
echo "    git status --porcelain is empty"

echo ""
echo "==> Fresh clone configured, built, tested and stayed clean."
