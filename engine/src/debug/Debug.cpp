// ============================================================================
//  Debug.cpp - the facade's non-template half.
//
//  Everything here is one of three things: a forward to eng::Log, a forward to
//  eng::Gizmos, or a small piece of state that has to live somewhere. The
//  state is what this file is really for - the default channel, the throttle
//  tables and the watch list's line cursor - and it lives in an anonymous
//  namespace here, reached only through the static members, which is how every
//  other piece of engine state is arranged. See the WINDOWS_EXPORT_ALL_SYMBOLS
//  note in engine/CMakeLists.txt for why that matters on Windows.
// ============================================================================

#include <engine/debug/Debug.h>

#include <engine/Engine.h>            // Engine::Get().Clock(), for Break
#include <engine/core/LogBuffer.h>    // ClearConsole
#include <engine/core/Time.h>         // FrameCount, UnscaledElapsedTime
#include <engine/render/Font.h>       // ForEachLine
#include <engine/render/Gizmos.h>
#include <engine/render/Renderer.h>
#include <engine/tools/GuiHooks.h>

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace eng {
namespace {

// ---------------------------------------------------------------------------
//  ONE LOCK FOR ALL OF IT.
//
//  Not because anything here is hot - it is a debug facade - but because
//  "any code anywhere can log" is the same assumption Log::Write is written
//  against, and a throttle table being rehashed by one thread while another
//  reads it is the kind of crash that only happens in somebody else's build.
//
//  It is never held while anything that could re-enter Debug is called. The
//  one place that was tempting - logging the key-limit warning - deliberately
//  happens after the lock is released.
// ---------------------------------------------------------------------------
std::mutex g_mutex;

std::string g_channel{Channels::kGame};

// LogOnce's keys, and LogEvery's keys with the time each last got through.
// Two tables rather than one, because the questions are different and a
// shared one would need a sentinel for "no time yet".
std::unordered_set<std::string>         g_once;
std::unordered_map<std::string, double> g_every;

// ---------------------------------------------------------------------------
//  HOW MANY KEYS ARE REMEMBERED BEFORE THE TABLES STOP GROWING.
//
//  A key is normally a format string written out in the source, so a real
//  game has a few dozen. A thousand means something is calling LogOnce with a
//  string it BUILT - "enemy 412 has no path" - and every distinct value is
//  its own key, which is a slow memory leak for as long as the game runs.
//
//  At the limit the message is let THROUGH rather than swallowed, and a
//  warning says why. Silence would be the worse failure: a LogOnce that
//  quietly stopped working looks exactly like code that is not running.
// ---------------------------------------------------------------------------
constexpr std::size_t kMaxKeys = 1024;
bool g_keyLimitReported = false;

Vec2               g_screenOrigin{10.0f, 10.0f};
Color              g_screenColor = Color::White();
unsigned long long g_screenFrame = ~0ull;   // no frame seen yet
std::size_t        g_screenLines = 0;

// Says the tables are full, once, and says it OUTSIDE the lock - logging
// while holding it is the one thing in this file that could re-enter.
void ReportKeyLimit() {
    {
        const std::lock_guard<std::mutex> lock{g_mutex};
        if (g_keyLimitReported) {
            return;
        }
        g_keyLimitReported = true;
    }
    ENGINE_LOG_WARN(Channels::kCore,
                    "Debug: {} distinct LogOnce/LogEvery keys - the throttle is "
                    "now letting everything through. A key is the FORMAT STRING, "
                    "so this many means something is passing a string it built "
                    "rather than one it wrote.",
                    kMaxKeys);
}

// ---------------------------------------------------------------------------
//  ONE GIZMO PER LINE, AND THE SPLITTING IS NOT DONE HERE.
//
//  Gizmos::Text goes straight to SDL_RenderDebugText, which draws a single
//  line and does nothing useful with a newline inside it: every line after
//  the first lands on top of the first. Renderer::DrawTextFont learned that
//  already, and the gizmo path never did - so anything feeding it has to
//  split the text first.
//
//  eng::ForEachLine in render/Font.h is where that is done for the whole
//  engine, and the reason it is one function rather than one per caller is
//  written at the top of it: it strips the \r out of a \r\n, which is the
//  part each caller forgets. Font.cpp, Renderer.cpp and UI.cpp all use it.
//  Writing a second copy here was a mistake the compiler caught, by making
//  the call ambiguous.
//
//  It calls back once per line and ALWAYS AT LEAST ONCE, so an empty string
//  still takes a line rather than silently doing nothing.
// ---------------------------------------------------------------------------

// Queues ONE line of the watch list and moves the cursor down.
void QueueScreenLine(std::string_view line) {
    const unsigned long long frame = Time::FrameCount();

    std::size_t index = 0;
    Vec2        origin{};
    Color       color{};
    {
        const std::lock_guard<std::mutex> lock{g_mutex};
        if (frame != g_screenFrame) {
            g_screenFrame = frame;
            g_screenLines = 0;
        }
        index  = g_screenLines++;
        origin = g_screenOrigin;
        color  = g_screenColor;
    }

    const float step = Renderer::TextLineHeight();
    Gizmos::Text(Vec2{origin.x, origin.y + step * static_cast<float>(index)},
                 std::string{line}, color, 0.0f, GizmoSpace::Screen,
                 GizmoCategory::Default);
}

} // namespace

// ---------------------------------------------------------------------------
//  The channel
// ---------------------------------------------------------------------------

std::string_view Debug::DefaultChannel() { return g_channel; }

void Debug::SetDefaultChannel(std::string_view channel) {
    const std::lock_guard<std::mutex> lock{g_mutex};
    if (channel.empty()) {
        g_channel.assign(Channels::kGame);
        return;
    }
    g_channel.assign(channel);
}

// ---------------------------------------------------------------------------
//  Writing
//
//  Log::Write tests the threshold again itself, so the plain-string overloads
//  need no test of their own here. The formatting ones in the header test it
//  BEFORE std::format runs, which is the whole point of them doing it there.
// ---------------------------------------------------------------------------

void Debug::Emit(LogLevel level, std::string_view message) {
    // A copy of the channel under the lock, rather than handing Log::Write a
    // view of a global that SetDefaultChannel could reassign underneath it.
    std::string channel;
    {
        const std::lock_guard<std::mutex> lock{g_mutex};
        channel = g_channel;
    }
    ::eng::Log::Write(channel, level, message);
}

void Debug::EmitIn(LogLevel level, std::string_view channel, std::string_view message) {
    ::eng::Log::Write(channel, level, message);
}

bool Debug::WillLog(LogLevel level) { return ::eng::Log::ShouldLog(level); }

// ---------------------------------------------------------------------------
//  The throttles
// ---------------------------------------------------------------------------

bool Debug::FirstTime(std::string_view key) {
    bool full = false;
    {
        const std::lock_guard<std::mutex> lock{g_mutex};

        // find() on a string_view key would need C++20 heterogeneous lookup,
        // which std::unordered_map does not have. One temporary string per
        // throttled call, on a path that exists to make calls rarer.
        const std::string owned{key};
        if (g_once.find(owned) != g_once.end()) {
            return false;
        }
        if (g_once.size() + g_every.size() >= kMaxKeys) {
            full = true;
        } else {
            g_once.insert(owned);
        }
    }

    if (full) {
        ReportKeyLimit();
    }
    return true;   // either it is genuinely the first time, or the table is full
}

bool Debug::DueNow(std::string_view key, float seconds) {
    const double now = Time::UnscaledElapsedTime();
    bool         full = false;
    bool         due  = false;
    {
        const std::lock_guard<std::mutex> lock{g_mutex};

        const std::string owned{key};
        const auto        found = g_every.find(owned);
        if (found != g_every.end()) {
            if (now - found->second < static_cast<double>(seconds)) {
                return false;
            }
            found->second = now;
            return true;
        }

        if (g_once.size() + g_every.size() >= kMaxKeys) {
            full = true;
        } else {
            g_every.emplace(owned, now);
        }
        due = true;
    }

    if (full) {
        ReportKeyLimit();
    }
    return due;
}

void Debug::Once(LogLevel level, std::string_view key, std::string_view message) {
    if (!::eng::Log::ShouldLog(level) || !FirstTime(key)) {
        return;
    }
    Emit(level, message);
}

void Debug::Every(LogLevel level, float seconds, std::string_view key,
                  std::string_view message) {
    if (!::eng::Log::ShouldLog(level) || !DueNow(key, seconds)) {
        return;
    }
    Emit(level, message);
}

void Debug::ResetThrottles() {
    const std::lock_guard<std::mutex> lock{g_mutex};
    g_once.clear();
    g_every.clear();
    g_keyLimitReported = false;
}

std::size_t Debug::ThrottleKeyCount() {
    const std::lock_guard<std::mutex> lock{g_mutex};
    return g_once.size() + g_every.size();
}

// ---------------------------------------------------------------------------
//  Drawing
//
//  Every one of these is a Gizmos call in GizmoCategory::Default - the group
//  the Scene view's Gizmos menu calls "Default", alongside whatever else game
//  code draws, rather than one of the engine's own groups.
// ---------------------------------------------------------------------------

void Debug::DrawLine(Vec2 from, Vec2 to, Color color, float duration) {
    Gizmos::Line(from, to, color, duration, GizmoSpace::World, GizmoCategory::Default);
}

void Debug::DrawRay(Vec2 origin, Vec2 direction, Color color, float duration) {
    Gizmos::Line(origin, origin + direction, color, duration, GizmoSpace::World,
                 GizmoCategory::Default);
}

void Debug::DrawArrow(Vec2 from, Vec2 to, Color color, float duration) {
    Gizmos::Line(from, to, color, duration, GizmoSpace::World, GizmoCategory::Default);

    const Vec2  along  = to - from;
    const float length = along.Length();
    if (length <= Vec2::kNormalizeEpsilon) {
        return;   // no direction, so no head to point anywhere
    }

    // A head a fifth of the line, capped so a long arrow does not grow a
    // comically large one and a short arrow still gets something visible.
    const float head = std::clamp(length * 0.2f, 3.0f, 14.0f);
    const Vec2  back  = along * (-head / length);
    const Vec2  side  = Vec2{-back.y, back.x} * 0.5f;

    Gizmos::Line(to, to + back + side, color, duration, GizmoSpace::World,
                 GizmoCategory::Default);
    Gizmos::Line(to, to + back - side, color, duration, GizmoSpace::World,
                 GizmoCategory::Default);
}

void Debug::DrawCircle(Vec2 centre, float radius, Color color, float duration) {
    Gizmos::Circle(centre, radius, color, duration, GizmoSpace::World,
                   GizmoCategory::Default);
}

void Debug::DrawBox(Vec2 centre, Vec2 size, Color color, float duration) {
    Gizmos::Box(AABB::FromCenterHalfExtents(centre, Vec2{size.x * 0.5f, size.y * 0.5f}),
                color, duration, GizmoSpace::World, GizmoCategory::Default);
}

void Debug::DrawBox(const AABB& box, Color color, float duration) {
    Gizmos::Box(box, color, duration, GizmoSpace::World, GizmoCategory::Default);
}

void Debug::DrawCross(Vec2 point, float size, Color color, float duration) {
    Gizmos::Line(Vec2{point.x - size, point.y}, Vec2{point.x + size, point.y}, color,
                 duration, GizmoSpace::World, GizmoCategory::Default);
    Gizmos::Line(Vec2{point.x, point.y - size}, Vec2{point.x, point.y + size}, color,
                 duration, GizmoSpace::World, GizmoCategory::Default);
}

void Debug::DrawText(Vec2 worldPosition, std::string_view text, Color color,
                     float duration) {
    // Split for the reason in ForEachLine above. The step down is in WORLD
    // units, because a world gizmo is queued long before anything knows what
    // camera will draw it - so the gap between two lines tightens and widens
    // with the zoom, where a screen-space line's does not. Y is up in this
    // engine, so later lines go to a SMALLER y.
    const float step = Renderer::TextLineHeight();
    float       drop = 0.0f;
    ForEachLine(text, [&](std::string_view line) {
        Gizmos::Text(Vec2{worldPosition.x, worldPosition.y - drop}, std::string{line},
                     color, duration, GizmoSpace::World, GizmoCategory::Default);
        drop += step;
    });
}

// ---------------------------------------------------------------------------
//  The watch list
//
//  The line cursor resets when the frame number changes rather than being
//  reset by the engine at a known point in the frame. That is not laziness:
//  a reset call would have to go somewhere in Engine::BeginFrame, and then
//  this file would only work inside a running engine - including in the
//  tests, which drive the clock by hand and never call BeginFrame on the
//  engine at all. GameClock::FrameCount increments on every frame, before the
//  pause test, so it ticks in the editor, while paused, and while not playing.
// ---------------------------------------------------------------------------

void Debug::ScreenText(std::string_view line) {
    ForEachLine(line, QueueScreenLine);
}

void Debug::Watch(std::string_view label, std::string_view value) {
    std::string line;
    line.reserve(label.size() + value.size() + 2);
    line.append(label).append(": ").append(value);
    ScreenText(line);
}

void Debug::SetScreenOrigin(Vec2 topLeft) {
    const std::lock_guard<std::mutex> lock{g_mutex};
    g_screenOrigin = topLeft;
}

Vec2 Debug::ScreenOrigin() {
    const std::lock_guard<std::mutex> lock{g_mutex};
    return g_screenOrigin;
}

void Debug::SetScreenColor(Color color) {
    const std::lock_guard<std::mutex> lock{g_mutex};
    g_screenColor = color;
}

Color Debug::ScreenColor() {
    const std::lock_guard<std::mutex> lock{g_mutex};
    return g_screenColor;
}

std::size_t Debug::ScreenLineCount() {
    const unsigned long long          frame = Time::FrameCount();
    const std::lock_guard<std::mutex> lock{g_mutex};
    return (frame == g_screenFrame) ? g_screenLines : 0;
}

void Debug::ResetScreenLines() {
    const std::lock_guard<std::mutex> lock{g_mutex};
    g_screenFrame = Time::FrameCount();
    g_screenLines = 0;
}

// ---------------------------------------------------------------------------
//  The editor, and what kind of build this is
// ---------------------------------------------------------------------------

bool Debug::IsEditorAttached() {
    // The editor installs these when its interface starts and clears them
    // when it stops - see EditorGui.cpp. The standalone player never sets
    // them, so a null ProcessEvent means "nothing is going to draw a Pause
    // button", which is the question Break actually needs answered.
    return GetGuiHooks().ProcessEvent != nullptr;
}

void Debug::Break() {
    if (!IsEditorAttached()) {
        LogWarning("Debug::Break() does nothing in a built game - there is no "
                   "Pause button to resume from. It stops the editor only.");
        return;
    }

    Engine& engine = Engine::Get();
    if (!engine.IsInitialised()) {
        return;
    }

    engine.Clock().SetPaused(true);
    Log("Debug::Break() - paused. Step on the Toolbar advances one frame.");
}

void Debug::ClearConsole() { LogBuffer::Clear(); }

bool Debug::IsDebugBuild() {
    // ENGINE_IS_DEBUG is set on the engine target only - see
    // engine/CMakeLists.txt - so this answers for the ENGINE, which is the
    // question. A project's scripts are compiled separately and a constant in
    // the header would have answered for them instead.
#if defined(ENGINE_IS_DEBUG) && ENGINE_IS_DEBUG
    return true;
#else
    return false;
#endif
}

} // namespace eng
