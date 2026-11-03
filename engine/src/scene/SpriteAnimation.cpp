// ============================================================================
//  SpriteAnimation.cpp - the flipbook component and the system that steps it.
//  See SpriteAnimation.h, which carries the reasoning.
// ============================================================================

#include <engine/core/Log.h>
#include <engine/resource/ResourceManager.h>
#include <engine/scene/SpriteAnimation.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace eng {
namespace {

// Every animation currently attached to something. The system walks this list
// rather than every entity in the scene asking "are you animated?".
std::vector<SpriteAnimationComponent*> g_animations;

} // namespace

// ---------------------------------------------------------------------------
//  FrameAt - the whole of the timing, over four numbers. See the header.
// ---------------------------------------------------------------------------
FlipbookPosition FrameAt(float elapsedSeconds, std::size_t frameCount,
                         float framesPerSecond, bool loop) {
    FlipbookPosition at;

    if (frameCount == 0) {
        return at;
    }

    // WRITTEN AS `!(x > 0)` RATHER THAN `x <= 0`, which is the same for every
    // number except a NaN - and a NaN is reachable, because a hand-edited
    // sidecar can say whatever it likes and nothing refuses a nonsense frame
    // rate. `!(nan > 0)` is true, so a NaN holds frame 0; `nan <= 0` is false,
    // so it would fall through to the arithmetic below and come out anywhere.
    if (!(framesPerSecond > 0.0f) || !(elapsedSeconds > 0.0f)) {
        return at;
    }

    // IN DOUBLE, AND COMPARED BEFORE IT IS CAST. A float elapsed time of 1e30
    // times a frame rate overflows a std::size_t, and converting a value too
    // large for the destination is undefined behaviour rather than a large
    // answer - so every path below either compares first or goes through fmod.
    const double advanced = static_cast<double>(elapsedSeconds) *
                            static_cast<double>(framesPerSecond);
    const double count = static_cast<double>(frameCount);

    if (!loop) {
        // THE WHOLE CLIP HAS PLAYED once `advanced` reaches the frame count:
        // the last frame is shown for its own frame-length first, and only
        // then is it finished. Stopping one frame early would make a two-frame
        // explosion show its second frame for no time at all.
        if (!(advanced < count)) {
            at.frame    = frameCount - 1;
            at.finished = true;
            return at;
        }
        at.frame = static_cast<std::size_t>(advanced);
        if (at.frame >= frameCount) {
            at.frame = frameCount - 1;   // rounding, at the very last instant
        }
        return at;
    }

    double wrapped = std::fmod(advanced, count);
    if (!(wrapped >= 0.0)) {
        wrapped = 0.0;   // a NaN that got this far, or a negative zero
    }
    at.frame = static_cast<std::size_t>(wrapped);
    if (at.frame >= frameCount) {
        at.frame = frameCount - 1;
    }
    return at;
}

// ---------------------------------------------------------------------------
//  SpriteAnimationComponent
// ---------------------------------------------------------------------------

SpriteAnimationComponent::~SpriteAnimationComponent() {
    // A safety net, the same one SpinComponent keeps: OnDetach is where the
    // removal normally happens, and this catches a component that was built
    // but never attached - which is what loading a scene that fails part way
    // through leaves behind.
    SpriteAnimationSystem::Unregister(*this);
}

bool SpriteAnimationComponent::Deserialize(const Json& node, std::string& outError) {
    // AN EMPTY PATH IS NOT AN ERROR, and that is deliberate: pressing "Add
    // Component" in the Inspector produces exactly this, and a component that
    // refused to load the state the editor itself creates would make the row
    // unusable. Resolve says what is missing, once, when something asks.
    m_path        = ReadString(node, "animation", "", kTypeName);
    m_speed       = ReadFloat(node, "speed", 1.0f, kTypeName);
    m_playOnStart = ReadBool(node, "playOnStart", true, kTypeName);

    if (!(m_speed > 0.0f)) {
        // CLAMPED RATHER THAN REFUSED, because a zero here means "frozen" to
        // whoever typed it and the component can honour that by not advancing.
        // Negative is not supported: nothing plays a flipbook backwards yet,
        // and silently reversing would be a feature nobody asked for.
        outError = "SpriteAnimationComponent.speed must be greater than zero; "
                   "using 1";
        m_speed  = 1.0f;
        return false;
    }

    // THE RESOLVE IS NOT DONE HERE. Scene loading attaches components in file
    // order, so the SpriteComponent beside this one may not exist yet, and the
    // sheet's .oimport may not have been read. Everything is worked out on the
    // first ask instead - see ResolvedFrames.
    m_generation = 0;
    return true;
}

bool SpriteAnimationComponent::Serialize(Json& out) const {
    out["animation"] = m_path;

    // WRITTEN ONLY WHEN THEY DIFFER FROM THE DEFAULT, so an ordinary animation
    // is one line in the scene file and a prefab override diff has one leaf
    // rather than three.
    if (m_speed != 1.0f) {
        out["speed"] = m_speed;
    }
    if (!m_playOnStart) {
        out["playOnStart"] = false;
    }
    return true;
}

void SpriteAnimationComponent::OnAttach() {
    SpriteAnimationSystem::Register(*this);

    // THE AUTHORED INTENT, read once. Nothing at run time writes
    // m_playOnStart, so this is the only place it decides anything.
    m_playing  = m_playOnStart;
    m_elapsed  = 0.0f;
    m_finished = false;
}

void SpriteAnimationComponent::OnDetach() {
    // THE SPRITE GETS ITS OWN PICTURE BACK. Not cosmetic: the component may be
    // removed in the editor from an entity that stays in the scene, and
    // leaving frame 7 on it would look exactly like somebody having set that
    // frame as the sprite's image - which the scene file does not say.
    RestoreAuthoredIfOurs();
    SpriteAnimationSystem::Unregister(*this);
}

bool SpriteAnimationComponent::SetAnimationPath(std::string_view subAssetPath) {
    if (!subAssetPath.empty() && !IsSubAsset(subAssetPath)) {
        // THE OLD CLIP IS LEFT ALONE. Half-applying this would stop the
        // animation that was working and put nothing in its place.
        SetProblem("'" + std::string(subAssetPath) +
                   "' is a whole image, not a part of one");
        return false;
    }

    RestoreAuthoredIfOurs();
    m_path.assign(subAssetPath);

    // REWOUND, AND PLAYING ONLY IF THAT IS WHAT THIS COMPONENT IS AUTHORED TO
    // DO. Choosing a different animation is not an instruction to start one:
    // playOnStart is the answer to that, and overriding it here would make the
    // tick box appear to have been forgotten.
    m_generation = 0;
    m_elapsed    = 0.0f;
    m_finished   = false;
    m_playing    = m_playOnStart;
    m_problem.clear();
    return true;
}

void SpriteAnimationComponent::SetSpeed(float multiplier) {
    m_speed = (multiplier > 0.0f) ? multiplier : 1.0f;
}

void SpriteAnimationComponent::SetPlayOnStart(bool play) {
    m_playOnStart = play;

    // AND IT TAKES EFFECT NOW. Unticking this in the editor can only mean "do
    // not play", and a box that appears to do nothing until the scene is
    // reloaded is a box somebody clicks twice and then distrusts.
    m_playing = play;
    if (!play) {
        return;
    }
    m_elapsed  = 0.0f;
    m_finished = false;
}

bool SpriteAnimationComponent::Play(std::string_view animationName) {
    // The sheet is this component's own, which is the whole point of taking a
    // NAME here - see the header. One resolve to learn it, if nothing has.
    if (m_sheetPath.empty()) {
        ResolvedFrames();
    }
    if (m_sheetPath.empty()) {
        SetProblem("this animation has no sheet yet, so '" +
                   std::string(animationName) + "' cannot be found");
        return false;
    }

    const std::string wanted = MakeSubAssetPath(m_sheetPath, animationName);
    if (wanted.empty()) {
        SetProblem("'" + std::string(animationName) +
                   "' is not a name this engine can use");
        return false;
    }

    const TextureImport   settings = ImportLibrary::ForTexture(m_sheetPath);
    const SheetAnimation* clip     = settings.sheet.FindAnimation(animationName);
    if (clip == nullptr) {
        SetProblem(m_sheetPath + " has no animation called '" +
                   std::string(animationName) + "'");
        return false;
    }

    // ======================================================================
    //  THE SAME CLIP ASKED FOR AGAIN EITHER CARRIES ON OR STARTS OVER, and
    //  the CLIP decides. See SheetAnimation::resume.
    //
    //  ONLY FOR THE ONE ALREADY LOADED, which is why this is guarded on the
    //  path rather than on the name alone: there is a single playback
    //  position, so switching to a different animation has nothing to carry
    //  on from and always begins at the start.
    //
    //  Play() RATHER THAN A FLAG OF ITS OWN, because it already draws the one
    //  distinction that matters here - a clip that has FINISHED starts again,
    //  and one merely paused picks up where it was. Restating that would be a
    //  second opinion about it.
    //
    //  NOTHING IS RE-RESOLVED on this path either: it is the clip already
    //  loaded, so the frames are the frames.
    // ======================================================================
    if (clip->resume && wanted == m_path) {
        Play();
        return true;
    }

    m_path       = wanted;
    m_generation = 0;   // the one spelling of "resolve again"
    Restart();
    return true;
}

void SpriteAnimationComponent::Play() {
    // A CLIP THAT HAD FINISHED STARTS AGAIN; one that was merely paused picks
    // up where it was. Those are the two things "play" can mean, and which one
    // applies is never ambiguous.
    if (m_finished) {
        m_elapsed  = 0.0f;
        m_finished = false;
    }
    m_playing = true;
}

void SpriteAnimationComponent::Restart() {
    m_elapsed  = 0.0f;
    m_finished = false;
    m_playing  = true;
}

void SpriteAnimationComponent::Pause() {
    m_playing = false;
}

void SpriteAnimationComponent::Stop() {
    m_playing  = false;
    m_elapsed  = 0.0f;
    m_finished = false;
    RestoreAuthoredIfOurs();
}

const std::vector<SheetFrame>& SpriteAnimationComponent::ResolvedFrames() {
    // ONE INTEGER COMPARE on an ordinary step. Generation starts at 1 and only
    // rises, so a component that has never resolved - m_generation of 0 -
    // always differs and always resolves on its first ask.
    if (m_generation != ImportLibrary::Generation()) {
        Resolve();
    }
    return m_resolvedFrames;
}

void SpriteAnimationComponent::Resolve() {
    m_generation = ImportLibrary::Generation();

    m_resolvedFrames.clear();
    m_frames.clear();
    m_fps          = 0.0f;
    m_loop         = false;
    m_resume       = false;
    m_framesListed = 0;
    m_sheetPath.clear();
    m_name.clear();

    if (m_path.empty()) {
        // NOT A PROBLEM WORTH LOGGING. This is what "Add Component" produces,
        // and the Inspector says "choose an animation" in its own words.
        m_problem.clear();
        return;
    }
    if (!IsSubAsset(m_path)) {
        SetProblem("'" + m_path + "' is a whole image, not a part of one");
        return;
    }

    m_sheetPath = SheetPathOf(m_path);
    m_name      = SubAssetNameOf(m_path);

    const TextureImport   settings  = ImportLibrary::ForTexture(m_sheetPath);
    const SheetAnimation* animation = settings.sheet.FindAnimation(m_name);
    if (animation == nullptr) {
        // THE THREE WAYS THIS GOES WRONG READ DIFFERENTLY, and telling them
        // apart is the difference between a fixable message and "it does not
        // work". Pointing a frame at this component is the likely mistake,
        // because a frame and an animation are the same kind of path.
        if (settings.sheet.FindFrame(m_name) != nullptr) {
            SetProblem("'" + m_name + "' is a single frame of " + m_sheetPath +
                       ", not an animation - animations are made in the "
                       "Inspector's Animations list");
        } else if (settings.sheet.IsSliced()) {
            SetProblem(m_sheetPath + " has no animation called '" + m_name + "'");
        } else {
            SetProblem(m_sheetPath + " has not been sliced into parts yet");
        }
        return;
    }

    m_fps          = animation->fps;
    m_loop         = animation->loop;
    m_resume       = animation->resume;
    m_framesListed = animation->frames.size();

    m_resolvedFrames.reserve(animation->frames.size());
    m_frames.reserve(animation->frames.size());

    for (const std::string& frameName : animation->frames) {
        const SheetFrame* frame = settings.sheet.FindFrame(frameName);
        if (frame == nullptr) {
            // SKIPPED, NOT REFUSED. An animation holds frame NAMES, and one
            // renamed away in the slicer leaves its old name behind here - so
            // the animation is quietly SHORTER rather than broken. The
            // Inspector reports the difference, because it has both counts and
            // is the only place somebody can act on it.
            continue;
        }
        m_resolvedFrames.push_back(*frame);

        // THE PIN. See the header: the texture cache is weak, so without this
        // a loop would re-cut a frame from the sheet every time it came round.
        m_frames.push_back(
            ResourceManager::LoadTexture(MakeSubAssetPath(m_sheetPath, frameName)));
    }

    if (m_resolvedFrames.empty()) {
        SetProblem("none of the frames '" + m_name + "' lists are in " +
                   m_sheetPath + " any more");
        return;
    }

    m_problem.clear();
}

void SpriteAnimationComponent::ShowFrame(std::size_t index) {
    if (index >= m_frames.size()) {
        return;
    }

    // ASKED FOR EVERY TIME, never remembered. The sprite can be removed from
    // the entity in the Inspector while this is playing, and a kept pointer
    // would then be the one thing in this file that could dangle. Find is a
    // string compare over the two to four components an entity has.
    SpriteComponent* sprite =
        (Owner() != nullptr) ? Owner()->Find<SpriteComponent>() : nullptr;
    if (sprite == nullptr) {
        if (!m_saidNoSprite) {
            m_saidNoSprite = true;
            ENGINE_LOG_WARN(Channels::kScene,
                            "'{}' has an animation but no SpriteComponent to play it "
                            "into, so nothing is drawn",
                            (Owner() != nullptr) ? Owner()->Name() : "<unattached>");
        }
        return;
    }
    m_saidNoSprite = false;

    // THE PICTURE THE SCENE FILE ASKED FOR, captured the first time it is
    // taken away rather than reloaded later from TexturePath(). Putting it
    // back is then a pointer copy that cannot fail and cannot log - which is
    // what makes it affordable in OnDetach, where a hundred dying animated
    // bullets would otherwise be a hundred fresh textures to fix a picture
    // nobody will see.
    if (m_shown == kAuthoredFrame) {
        m_authoredTexture = sprite->GetTexture();
    }

    // ONE POINTER COMPARE, and it skips most steps: at twelve frames a second
    // against a sixty-hertz step, five steps out of six ask for the frame
    // already on screen. It also self-corrects if something else changed the
    // sprite's picture underneath us.
    if (sprite->GetTexture() != m_frames[index]) {
        sprite->ShowTexture(m_frames[index]);
    }
    m_shown = index;
}

void SpriteAnimationComponent::RestoreAuthoredIfOurs() {
    if (m_shown == kAuthoredFrame) {
        return;   // never took it away
    }

    SpriteComponent* sprite =
        (Owner() != nullptr) ? Owner()->Find<SpriteComponent>() : nullptr;
    if (sprite != nullptr) {
        // ONLY IF IT IS STILL SHOWING ONE OF OUR FRAMES. A script or a
        // drag-and-drop may have set the sprite's picture since, and putting
        // the authored one back over that would undo somebody else's change.
        const TextureRef& showing = sprite->GetTexture();
        const bool        ours =
            std::find(m_frames.begin(), m_frames.end(), showing) != m_frames.end();
        if (ours) {
            sprite->ShowTexture(m_authoredTexture);
        }
    }

    m_shown = kAuthoredFrame;
    m_authoredTexture.reset();
}

std::string_view SpriteAnimationComponent::CurrentFrameName() const {
    if (m_shown >= m_resolvedFrames.size()) {
        return {};
    }
    return m_resolvedFrames[m_shown].name;
}

void SpriteAnimationComponent::SetProblem(std::string reason) {
    m_problem = std::move(reason);
    if (m_problem.empty() || m_problem == m_problemLogged) {
        return;
    }

    // ONCE PER DISTINCT PROBLEM, not once per component and not once per
    // resolve. See the header: re-slicing can break a working animation for a
    // NEW reason, which is exactly when somebody needs telling, while the
    // editor drops the whole import cache on every window focus, which would
    // otherwise reprint every line on every alt-tab.
    m_problemLogged = m_problem;
    ENGINE_LOG_WARN(Channels::kScene, "the animation on '{}' is not playing: {}",
                    (Owner() != nullptr) ? Owner()->Name() : "<unattached>",
                    m_problem);
}

void SpriteAnimationComponent::Step(float fixedDeltaSeconds) {
    if (!m_playing) {
        return;
    }

    const std::vector<SheetFrame>& frames = ResolvedFrames();
    if (frames.empty()) {
        return;   // Resolve has already said why, once
    }

    m_elapsed += fixedDeltaSeconds;

    // THE SHEET'S RATE TIMES THIS ENTITY'S MULTIPLIER, worked out here rather
    // than stored, so that editing the sheet's fps is felt on the next step.
    const FlipbookPosition at =
        FrameAt(m_elapsed, frames.size(), m_fps * m_speed, m_loop);

    ShowFrame(at.frame);

    if (at.finished) {
        // STOPPED ON ITS LAST FRAME. Nothing is destroyed and nothing is
        // reported - that is what "played once" means - and IsFinished stays
        // true until something plays it again, so a script can poll it.
        m_finished = true;
        m_playing  = false;
    }
}

// ---------------------------------------------------------------------------
//  SpriteAnimationSystem
// ---------------------------------------------------------------------------

void SpriteAnimationSystem::Register(SpriteAnimationComponent& animation) {
    g_animations.push_back(&animation);
}

void SpriteAnimationSystem::Unregister(SpriteAnimationComponent& animation) {
    std::erase(g_animations, &animation);
}

void        SpriteAnimationSystem::Clear() { g_animations.clear(); }
std::size_t SpriteAnimationSystem::Count() { return g_animations.size(); }

void SpriteAnimationSystem::Update(float deltaSeconds) {
    // Walked by index with the size re-read, because a script's OnFixedUpdate
    // runs at this same stage and may add an animation to something.
    for (std::size_t i = 0; i < g_animations.size(); ++i) {
        SpriteAnimationComponent* animation = g_animations[i];
        if (animation == nullptr || !animation->OwnerIsActive()) {
            continue;
        }

        // deltaSeconds is the FIXED step, handed down by the scheduler. Asking
        // a clock here instead is what would make a flipbook play at different
        // speeds on different machines.
        animation->Step(deltaSeconds);
    }
}

void SpriteAnimationSystem::RegisterComponentTypes() {
    ComponentFactory::Register(SpriteAnimationComponent::kTypeName,
                               []() -> std::unique_ptr<Component> {
                                   return std::make_unique<SpriteAnimationComponent>();
                               });
}

} // namespace eng
