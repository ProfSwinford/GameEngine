#pragma once

// ============================================================================
//  SpriteAnimation.h - plays one of a spritesheet's stored animations into the
//  SpriteComponent beside it, one frame at a time.
//
//  THE ANIMATION IS NOT HERE. It lives in the sheet's .oimport as a name and a
//  list of frame NAMES - see resource/ImportSettings.h. This component holds
//  one path into it:
//
//      { "type": "SpriteAnimationComponent",
//        "animation": "textures/hero.png#walk" }
//
//  Nothing is copied out of the sheet into the scene file, so re-slicing,
//  renaming a frame or changing the frame rate is an edit to ONE file and
//  every entity playing it follows.
//
//  ==========================================================================
//  THE RULE THIS FILE IS ORGANISED AROUND: A SYSTEM MUST NEVER WRITE A FIELD
//  THAT GETS SAVED.
//
//  SpriteComponent::SetTexture writes m_texturePath, Serialize writes that
//  into the scene file, and Ctrl+S is NOT blocked while the game is playing -
//  unlike New Scene. So playback through SetTexture would write
//  "textures/hero.png#walk_7" into the .oscene as the sprite's authored
//  picture, permanently, and silently, because a frame path loads perfectly
//  well; Stop would then restore the authored value in memory from the
//  play-mode snapshot, leaving the file and the editor disagreeing with
//  nothing anywhere saying so. Undo is the same path - every snapshot is a
//  full Scene::SaveToString.
//
//  Hence SpriteComponent::ShowTexture, which changes the PICTURE and not the
//  PATH. The same rule decides every field below: the saved ones are read by
//  this component and written by nobody, which is why there is a
//  "playOnStart" and deliberately NOT a "playing".
//
//  ==========================================================================
//  IT RESOLVES AGAINST THE SHEET WHENEVER THE SHEET MIGHT HAVE CHANGED, AND
//  NOT OTHERWISE.
//
//  A frame name is a NAME: slicing again, renaming a frame or hand-editing the
//  sidecar can change what it means or delete it outright, underneath a
//  playing animation - and ResourceManager::InvalidateSubAssets erases cache
//  ENTRIES and cannot reach a texture something already holds, which the
//  vector below is. ImportLibrary::Generation() is one integer that moves
//  whenever any answer from that library might have changed. This component
//  remembers the value it resolved at and asks again when they differ: one
//  integer compare on an ordinary step.
//
//  THE DEFINITION AND THE PICTURES ARE RESOLVED TOGETHER, IN ONE FUNCTION,
//  and that is the point rather than a tidiness. Re-reading the frame LIST
//  without re-cutting the pictures - which is the shape an obvious "pick up
//  the new fps" change would take - would advance through the new names while
//  showing pixels cut from the old slice. Names and pictures out of step, with
//  nothing wrong on screen to explain it.
//
//  ==========================================================================
//  THE FRAMES ARE PINNED, AND THAT IS NOT AN OPTIMISATION.
//
//  ResourceManager's texture cache holds WEAK references - the sprites own the
//  textures and the cache only remembers where they are - and the frame loop
//  prunes expired entries every frame. A loop that held only the frame on
//  screen would re-cut a frame from the sheet on every change FOR EVER: a
//  fresh SDL texture, an upload and a "loaded '...'" line twelve times a
//  second, per sprite. m_frames holds every frame of the resolved animation,
//  so an ordinary step makes no call into ResourceManager at all.
//
//  ==========================================================================
//  WHAT THIS CANNOT DO. Said here, and said again in the Inspector, because a
//  feature that implies more than it does generates bug reports instead of
//  questions.
//
//    1. IT DOES NOT PLAY IN THE EDITOR. This is a kGameplay system driven by
//       the fixed step, and a paused clock asks for no steps. Press Play and
//       both views animate; Pause and Step then advance it one frame at a
//       time, which is the only frame-by-frame animation viewer here.
//    2. ONE ANIMATION, NO SWITCHING IN DATA. A script can call Play("jump"),
//       but there is no blending, no state machine and no transitions - which
//       is why this is a SpriteAnimation and not an "Animator", a word that
//       promises all three.
//    3. A NON-LOOPING ANIMATION HOLDS ITS LAST FRAME WHEN IT ENDS. Nothing is
//       destroyed and nothing is reported: that is what "played once" means.
//       Poll IsFinished() in OnFixedUpdate to act on it.
//    4. EVERY COPY STARTS TOGETHER. Forty enemies loaded from one scene file
//       resolve on the same step with no elapsed time, so they animate in
//       LOCKSTEP. There is no start offset. One field would fix it; nobody has
//       asked yet, and a field nobody has asked for is one that has to be
//       loaded, saved, diffed against a prefab and explained.
// ============================================================================

#include <engine/render/Texture.h>
#include <engine/resource/ImportSettings.h>
#include <engine/scene/AnimationRef.h>
#include <engine/scene/Component.h>
#include <engine/scene/SystemOrder.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eng {

// ---------------------------------------------------------------------------
//  WHICH FRAME A FLIPBOOK IS SHOWING after this much of it has played.
//
//  A FREE FUNCTION OVER NUMBERS, and deliberately not a member: no component,
//  no sheet, no texture, no renderer. That is what makes the awkward cases
//  testable with no window on screen - the same argument SliceGrid makes in
//  resource/ImportSettings.h - and it matters here because the test binary has
//  no renderer and therefore cannot create a single frame.
//
//  `framesPerSecond` is the EFFECTIVE rate: the sheet's fps times the
//  component's speed. A rate of zero or less, or a NaN, HOLDS FRAME 0 rather
//  than dividing by it. Not defensiveness - ImportSettings.h declines to
//  refuse a nonsense frame rate on purpose and says in as many words that
//  whatever plays an animation has to cope, because a hand-edited sidecar can
//  still say 0.
//
//  TOTAL FOR EVERY INPUT. The wrap is computed in double and the result is
//  compared before it is cast, so an elapsed time of 1e30 gives a frame in
//  range instead of undefined behaviour.
// ---------------------------------------------------------------------------
struct FlipbookPosition {
    std::size_t frame    = 0;
    bool        finished = false;   // a non-looping run that reached its end
};

FlipbookPosition FrameAt(float elapsedSeconds, std::size_t frameCount,
                         float framesPerSecond, bool loop);

class SpriteAnimationComponent final : public Component {
public:
    static constexpr const char* kTypeName = "SpriteAnimationComponent";

    // CurrentFrame() when the sprite is showing the picture its own "texture"
    // field names - which in edit mode is for ever, and is how the Inspector
    // knows to say "press Play" rather than inventing a frame number.
    static constexpr std::size_t kAuthoredFrame = static_cast<std::size_t>(-1);

    SpriteAnimationComponent() = default;
    ~SpriteAnimationComponent() override;

    // ONLY EVER OWNED THROUGH Entity's list of components. The system holds
    // raw pointers, and a moved-from component would leave two addresses where
    // the system knows one.
    SpriteAnimationComponent(const SpriteAnimationComponent&)            = delete;
    SpriteAnimationComponent& operator=(const SpriteAnimationComponent&) = delete;

    const char* TypeName() const override { return kTypeName; }

    // Scene file fields:
    //   "animation":   "textures/hero.png#walk"  required; may be empty
    //   "speed":        1.0   a MULTIPLIER on the sheet's fps; omitted at 1
    //   "playOnStart":  true  omitted when true
    //
    // THE FRAME RATE AND THE REPEAT FLAG ARE DELIBERATELY NOT FIELDS. They
    // belong to the animation, in the sheet's .oimport, which is where the
    // Inspector's own "Frames a second" box and "Repeat for ever" tick write
    // them. A copy here would be a second answer to a question the sidecar
    // already answers, and the panel would go on editing the first one - the
    // mistake ImportSettings.h refuses by name, in its worse form, because the
    // copy WOULD be honoured, by this component and by nothing else on screen.
    // Looping is also a property of the animation's MEANING rather than of the
    // entity: a walk loops, an explosion does not, and a per-entity flag makes
    // "an explosion that repeats for ever" an authorable mistake forty times
    // over. "speed" composes instead of contradicting.
    bool Deserialize(const Json& node, std::string& outError) override;
    bool Serialize(Json& out) const override;

    void OnAttach() override;
    void OnDetach() override;

    // ---- what it plays, which is what gets saved --------------------------
    const std::string& AnimationPath() const { return m_path; }

    // The halves of that path, filled in by the last resolve. Empty until
    // something has asked ResolvedFrames() at least once.
    const std::string& AnimationName() const { return m_name; }
    const std::string& SheetPath() const { return m_sheetPath; }

    // Points this at a different animation and REWINDS it, playing only if
    // this component is authored to play on start - choosing a clip is not an
    // instruction to start one, and overriding playOnStart here would make
    // that tick box look forgotten. A script that wants it playing calls
    // Play(name), which is the door built for that.
    //
    // False, with Problem() set, when the path does not name a part of an
    // image - in which case the old clip is LEFT ALONE rather than half
    // replaced.
    bool SetAnimationPath(std::string_view subAssetPath);

    float Speed() const { return m_speed; }
    void  SetSpeed(float multiplier);

    // Changing this in the Inspector also decides whether it is playing RIGHT
    // NOW: unticking it in edit mode can only mean "do not play", and the
    // alternative is a tick box that appears to do nothing until the scene is
    // reloaded. NOTHING AT RUN TIME CALLS THIS - see the rule at the top.
    bool PlayOnStart() const { return m_playOnStart; }
    void SetPlayOnStart(bool play);

    // ======================================================================
    //  WHAT A SCRIPT CALLS. Reached the ordinary way:
    //
    //      void OnStart() override {
    //          if (auto* anim = Find<eng::SpriteAnimationComponent>()) {
    //              anim->Play("idle");
    //          }
    //      }
    //      void OnFixedUpdate(float) override {
    //          if (auto* anim = Find<eng::SpriteAnimationComponent>();
    //              anim != nullptr && anim->IsFinished()) {
    //              DestroySelf();
    //          }
    //      }
    //
    //  DO NOT KEEP THE POINTER. Ask again each time, the same rule
    //  ScriptComponent::Behaviour states and for the same reason: the
    //  component dies with its entity, and the whole scene is rebuilt by Play,
    //  by Stop and by every undo step.
    //
    //  Play TAKES A NAME, NOT A PATH, and that is the point of it. A script
    //  writing Play("textures/hero.png#jump") repeats the sheet's path at
    //  every call site and breaks - in code, not in data - the day the sheet
    //  is renamed. This component already knows the sheet, so "jump" is all
    //  there is to say. It restarts even when the name is the one already
    //  playing, so hitting something twice flashes twice.
    //
    //  False, with ONE message naming the sheet and the name, when there is no
    //  such animation. Refusing silently is how "my jump animation does
    //  nothing" becomes unanswerable from the screen.
    //
    //  NOTE THAT THIS WRITES A SAVED FIELD, and that is allowed where a system
    //  doing it is not: a script writing a field is the game's own code making
    //  a deliberate change, the same as SetTint or SetLayer. Twelve writes a
    //  second of internal state is the thing the rule at the top forbids.
    // ======================================================================
    bool Play(std::string_view animationName);

    // ======================================================================
    //  THE SAME, THROUGH AN EXPOSED AnimationRef - which is how a script
    //  should choose a clip, because then it is chosen by dragging in the
    //  Inspector instead of typed into the code.
    //
    //  IT ABSORBS THE DECISION EVERY CALLER WAS MAKING BY HAND. A reference
    //  holds a WHOLE sub-asset path; this component is already playing from
    //  one sheet. The two cases want different calls and are not
    //  interchangeable:
    //
    //    a path naming THIS sheet is reduced to Play(name), which keeps the
    //    frames already cut, pre-validates the clip and honours the clip's
    //    own `resume` flag;
    //
    //    a path naming ANOTHER sheet pays for SetAnimationPath and is then
    //    asked to play, because SetAnimationPath honours the component's
    //    "play on start" tick - which is the authored intent for the FIRST
    //    clip and not for this one. Getting that half wrong leaves an entity
    //    changing direction and standing still.
    //
    //  A BARE NAME IS STILL HONOURED, because the text box this type replaces
    //  took one and scenes saved before the type existed may still hold one.
    //
    //  AND IT DOES NOT ASK SheetPath(), which is the bug it exists to stop
    //  being repeated. SheetPath() is empty until something has called
    //  ResolvedFrames(), and on an entity that is not playing yet nothing
    //  has - Step returns before it. A script comparing against it therefore
    //  always missed, and every change of direction re-read the sheet and
    //  re-loaded every frame. assets/scripts/TilemapWalker.cpp did exactly
    //  that, under a comment explaining that it was avoiding the cost. This
    //  compares against SheetPathOf(m_path) instead, which is answerable with
    //  no resolve at all because m_path is a plain member.
    //
    //  AN UNSET REFERENCE DOES NOTHING AND SAYS NOTHING - deliberately
    //  unlike a missing prefab. A blank animation slot is a direction a
    //  character does not have; a side-on sprite has no up or down, and
    //  walking up should keep whatever was already playing. Warning would
    //  log once per such entity per change of direction.
    //
    //  False when nothing could be played, with Problem() set by whichever
    //  call refused. The clip that was playing is left alone.
    // ======================================================================
    bool Play(const AnimationRef& animation);

    void Play();      // resume; rewinds first only if it had finished
    void Restart();   // back to the first frame, playing
    void Pause();     // stop advancing and KEEP the picture on screen
    void Stop();      // stop, rewind, and put the AUTHORED picture back

    bool IsPlaying() const { return m_playing; }

    // A non-looping clip that reached its last frame, and stayed on it. Stays
    // true until something plays it again.
    bool IsFinished() const { return m_finished; }

    // ======================================================================
    //  THE ONE PLACE THE SHEET IS READ. Resolves if this is the first ask or
    //  if ImportLibrary has moved since the last one, then hands back the
    //  frames that actually exist, in order.
    //
    //  NOT const, because it may resolve. ASK THIS FIRST and read everything
    //  below it afterwards: SheetFps, Loops, FramesListed and Problem are
    //  plain reads of whatever this last found.
    //
    //  Called by the system every step AND by the Inspector every frame it
    //  draws - deliberately the same function, so that what the panel reports
    //  and what is on screen cannot be two answers, and so that the panel is
    //  live against the slicer in edit mode, where the system does not run at
    //  all.
    //
    //  SheetFrame RATHER THAN A BARE NAME, because the rectangle earns its
    //  place twice over. It is what tells a re-slice from an unrelated save:
    //  SheetFrame compares by name AND rectangle, which is exactly what a
    //  frame texture is a function of - so dropping the whole import cache,
    //  which the editor does on every window focus, costs one map lookup and
    //  one vector compare instead of re-cutting every frame in the scene.
    //
    //  AND THE NAMES ARE THE AUTHORITY, NOT THE TEXTURES. LoadTexture never
    //  returns empty - it hands back the magenta placeholder - and in the test
    //  binary, which has no renderer, that placeholder is itself empty. So
    //  "m_frames is not empty" is NOT "the animation resolved", and every
    //  stepping decision below is written against this list, which is
    //  reachable from a test with no renderer at all.
    // ======================================================================
    const std::vector<SheetFrame>& ResolvedFrames();

    // The sheet's own numbers for the resolved animation; 0 and false when
    // nothing resolved.
    float SheetFps() const { return m_fps; }
    bool  Loops() const { return m_loop; }

    // Whether asking for this clip again carries on from where it was rather
    // than starting over. The CLIP's own answer, read in Resolve with the two
    // above - see SheetAnimation::resume for why it lives there and not here.
    bool Resumes() const { return m_resume; }

    // How many names the animation LISTS, against ResolvedFrames().size()
    // which is how many still exist. The two differ when a frame has been
    // renamed away: a name that matches nothing is SKIPPED, so the animation
    // is quietly SHORTER, which is a warning rather than a break.
    std::size_t FramesListed() const { return m_framesListed; }

    // Which entry of ResolvedFrames is on the sprite, or kAuthoredFrame.
    std::size_t      CurrentFrame() const { return m_shown; }
    std::string_view CurrentFrameName() const;

    // ======================================================================
    //  WHY NOTHING IS MOVING, in words, or empty when nothing is wrong.
    //
    //  ONE STATEMENT OF THE DIAGNOSIS, written where it is discovered and read
    //  by everything that reports it: the Console line said once, the
    //  Inspector's red text, and the message a drag-and-drop puts in the log.
    //  The alternative is the editor working the answer out again from
    //  ImportLibrary, which is a second answer that can disagree with the
    //  first.
    // ======================================================================
    const std::string& Problem() const { return m_problem; }

    // Called by SpriteAnimationSystem only.
    void Step(float fixedDeltaSeconds);

private:
    void Resolve();                       // unconditional; ResolvedFrames decides when
    void ShowFrame(std::size_t index);
    void RestoreAuthoredIfOurs();
    void SetProblem(std::string reason);  // logs it, ONCE per distinct text

    // ---- saved ------------------------------------------------------------
    std::string m_path;
    float       m_speed       = 1.0f;
    bool        m_playOnStart = true;

    // ---- derived from the path, in Resolve --------------------------------
    std::string m_sheetPath;
    std::string m_name;

    // ---- read from the sheet, in Resolve, all at one moment ---------------
    std::vector<SheetFrame> m_resolvedFrames;   // the survivors, in order
    std::vector<TextureRef> m_frames;           // PINNED; same size, same order
    float                   m_fps          = 0.0f;
    bool                    m_loop         = false;
    bool                    m_resume       = false;
    std::size_t             m_framesListed = 0;

    // The ImportLibrary generation the two vectors above were built against.
    // ZERO IS THE ONE SPELLING OF "ask again": Generation() starts at 1 and
    // only ever rises, so a fresh component resolves on its first ask, and
    // SetAnimationPath forces a resolve by putting this back to 0 rather than
    // by carrying a second "dirty" flag that could disagree with it.
    std::uint64_t m_generation = 0;

    // ---- the cursor, which NOTHING saves ----------------------------------
    //
    // If the frame index were a field, the play-mode snapshot would capture it
    // and Stop would restore the scene mid-clip - the thing the transient
    // setter exists to prevent, arriving by another door.
    float       m_elapsed  = 0.0f;
    std::size_t m_shown    = kAuthoredFrame;
    bool        m_playing  = false;
    bool        m_finished = false;

    // The picture the sprite had the first time we took it away. CAPTURED
    // RATHER THAN RELOADED FROM TexturePath(): putting it back is then a
    // pointer copy that cannot fail and cannot log, instead of a LoadTexture
    // that misses the cache - which is what makes it affordable in OnDetach,
    // where a hundred dying animated bullets would otherwise be a hundred
    // fresh GPU textures to fix a picture nobody will see.
    TextureRef m_authoredTexture;

    // ---- said once, and once means once per DISTINCT problem --------------
    //
    // Not once per component: re-slicing can break an animation that was fine,
    // for a NEW reason, which is the moment somebody most needs telling. Not
    // once per resolve either: the editor drops the import cache every time
    // its window regains focus, so that would reprint every line on every
    // alt-tab. Comparing the TEXT gets both right for the price of one string.
    std::string m_problem;
    std::string m_problemLogged;

    // A SECOND flag rather than reusing m_problem, because the two are cured
    // by different things - a bad animation by editing the path, a missing
    // sprite by adding one - and one flag would swallow whichever message came
    // second.
    bool m_saidNoSprite = false;
};

// Advances every attached SpriteAnimationComponent, once per simulation step.
//
// kGameplay (200), the FIXED step, and the reasons are worth writing down
// because the per-frame route looks tempting:
//
//   - DETERMINISM. deltaSeconds is the FIXED step handed down by the
//     scheduler, so a flipbook shows the same frame on the same tick on every
//     machine. See the note on System::Update in SystemOrder.h.
//   - THE EDIT-MODE GATE COMES FREE. The clock is paused outside play mode, a
//     paused clock asks for zero steps, so this does not run while you are
//     editing - which is what keeps a playing animation from ever touching a
//     scene you are in the middle of authoring.
//   - NO EDITOR DOUBLE-TICK. Stages 800 and up run from the render path, which
//     the editor calls TWICE - once for the Game view and once for the Scene
//     view - so a render-stage system would animate at twice the rate in the
//     editor and once in the player.
//
// WHAT IT COSTS, both ways, and both are correct for a game animation: frame
// timing quantises to the fixed step (12 fps against a 60 Hz step is exactly 5
// steps a frame, worst case one step of error), and a time scale of zero runs
// no steps at all, so animations freeze behind a pause menu while OnUpdate
// keeps running.
//
// REGISTERED AFTER ScriptSystem in Engine.cpp, because ties at one stage keep
// registration order and a script that calls Play("jump") in OnFixedUpdate
// should be honoured in that step rather than the next one.
class SpriteAnimationSystem final : public System {
public:
    void        Update(float deltaSeconds) override;
    const char* Name() const override { return "SpriteAnimationSystem"; }
    int         Order() const override { return SystemStage::kGameplay; }

    static void        Register(SpriteAnimationComponent& animation);
    static void        Unregister(SpriteAnimationComponent& animation);
    static void        Clear();
    static std::size_t Count();

    // Tells the ComponentFactory that "SpriteAnimationComponent" in a scene
    // file means this class.
    static void RegisterComponentTypes();
};

} // namespace eng
