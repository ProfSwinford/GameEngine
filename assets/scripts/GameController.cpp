// =============================================================================
//  GameController - one controller, one fade, across a scene change.
//
//      controller->GoToScene("scenes/collector.oscene");
//
//  The screen fades to black, the old scene is torn down, the new one loads,
//  and the screen fades back. Nothing the player sees jumps.
//
//  =============================================================================
//  THIS CARRIES ITS STATE IN STATICS, AND NO LONGER HAS TO
//
//  DontDestroyOnLoad() NOW EXISTS - call it on a script's own entity and the
//  entity, its components, its behaviour and its children survive a scene
//  change as the same objects. See ScriptBehaviour::DontDestroyOnLoad and
//  Scene::SetPersistent. This file predates it and is left as it was, because
//  it works and because the static approach is still the right one for what
//  it happens to need: THREE FLOATS AND NOTHING ELSE.
//
//  WHICH IS THE WHOLE DISTINCTION WORTH TAKING FROM THIS FILE. Statics carry
//  STATE. They cannot carry a position, a child, a coroutine, a collider or a
//  reference that something else holds - and the moment a manager needs any
//  of those, the entity has to survive rather than its numbers. A fade is a
//  float; an inventory the player is carrying is not.
//
//  So the ENTITY does not survive here and the STATE does. The three statics
//  below live as long as the script library is loaded, and a GameController in
//  the next scene adopts them in OnStart rather than starting fresh. What a
//  player can see - the black screen - is therefore continuous even though the
//  object drawing it is not the same object.
//
//  REWRITTEN WITH DontDestroyOnLoad this would be one controller that is never
//  torn down, and the statics would go. That is a better shape and a change to
//  working content, so it has not been made here.
//
//  WHY THAT IS NOT A FLASH OF THE NEW LEVEL. The load happens at the END of
//  the frame that asked for it, and the new scene's OnStart runs before its
//  first draw. The new controller has already put the overlay back to full
//  black by the time anything is rendered, so there is no frame in between for
//  the player to see through.
//
//  =============================================================================
//  EVERY SCRIPT IS ITS OWN DLL, WHICH IS WHY THE PUBLIC API TAKES NO STATICS
//
//  THE TRAP THIS FILE IS SHAPED AROUND, and it cost a wrong first version.
//
//  The build compiles each script into a SEPARATE library - 26 scripts, 26
//  DLLs in .build/modules - and the generated scope header puts every script's
//  source into all of them. So each DLL gets its OWN copy of this class,
//  including its own copy of every static member. A function defined in the
//  class body is compiled into whichever DLL CALLS it, so a static touched
//  through such a call reads the caller's copy, not this one's.
//
//      GameController::Instance()           // from another script: always null
//      controller->SomeInlineStaticReader() // reads the CALLER's static
//
//  Both compile, neither warns, and the first version of this file shipped
//  `static Instance()` as its public API and returned null from every script
//  that asked.
//
//  WHAT IS SAFE. The hooks and the coroutines below are reached through the
//  dispatcher the build generates beside ENGINE_REGISTER_SCRIPT, which lives
//  in THIS library - so OnStart, OnUpdate, OnDestroy and both coroutines all
//  read the right statics. INSTANCE members are safe too: they live in the
//  object rather than in a library.
//
//  So the public API below writes instance members only, and OnUpdate - which
//  does run here - is what turns a request into a fade. That is the whole
//  reason GoToScene does not simply start the coroutine itself.
//
//  =============================================================================
//  THE SINGLETON IS A GUARD, NOT A PROMISE
//
//  OnStart destroys its own entity if another controller already holds the
//  slot, which is Unity's Awake() pattern, and it works because every
//  GameController is created by this same library. It cannot stop somebody
//  writing a second one for the frame it takes to notice - nothing in C++ can.
//
//  =============================================================================
//  WHY THE OVERLAY IS A UI ENTITY AND NOT A Renderer:: CALL
//
//  A script's OnUpdate runs at SystemStage::kGameplay (200); rendering is 800.
//  A Renderer::DrawFilledRect from a script therefore lands UNDER everything
//  drawn afterwards - the fade would be behind the game, which is no fade at
//  all. Gizmos are drawn late enough (900) but the editor switches them off
//  per panel, so a gizmo fade would be invisible in the very view somebody
//  tests it in. UISystem::Render runs after the world in both the editor and
//  the player, and sortOrder puts this above the rest of the interface.
// =============================================================================

#include <engine/Engine.h>


class GameController final : public eng::ScriptBehaviour {
public:
    // A PUBLIC FIELD IS AN INSPECTOR CONTROL, and these are live while the
    // game runs - so the fade can be tuned without a rebuild.
    float fadeOutSeconds = 0.40f;   // going to black
    float fadeInSeconds  = 0.40f;   // coming back

    // ---- the API other scripts use ------------------------------------------
    //
    //  SAFE TO CALL FROM ANOTHER SCRIPT because it writes instance members and
    //  nothing else. The work happens in OnUpdate, which runs in this library.
    //  See the note above on why that distinction is load-bearing here.
    //
    //  Reach a controller the way DoorSwitch reaches its Door - hold
    //  `GameController controller;` and drag the entity onto it - or ask the
    //  scene with eng::FindScriptIn<GameController::ScriptType>(*GetScene()).
    void GoToScene(std::string_view scenePath) {
        if (scenePath.empty()) {
            return;
        }
        m_pendingScene.assign(scenePath);
        m_hasPending = true;
    }

    // An instance member rather than a static read, for the same reason.
    // Mirrored from the static every frame by OnUpdate.
    bool IsTransitioning() const { return m_transitioning; }

    void OnStart() {
        // THE GUARD, FIRST. Destroy is queued rather than immediate, so this
        // object lives out the frame - which is why it returns here instead of
        // carrying on to claim the slot it just lost.
        if (s_instance != nullptr && s_instance != this) {
            ENGINE_LOG_INFO(eng::Channels::kGame,
                            "GameController: one already exists; removing the duplicate on '{}'",
                            Name());
            Destroy(*Owner());
            return;
        }
        s_instance = this;

        BuildOverlay();

        // ARRIVING MID-TRANSITION. The controller that asked for this scene is
        // gone, but it left the phase behind. Put the screen back to black
        // before anything is drawn, then bring it up.
        if (s_phase == Phase::FadingIn) {
            s_alpha = 1.0f;
            PushAlpha();
            StartCoroutine(FadeIn());
        } else {
            s_alpha = 0.0f;
            PushAlpha();
        }
        m_transitioning = (s_phase != Phase::Idle);
    }

    // Only the instance that OWNS the slot clears it. A duplicate removed by
    // the guard above also runs this, and without the test it would blank the
    // pointer belonging to the controller that beat it.
    void OnDestroy() {
        if (s_instance == this) {
            s_instance = nullptr;
        }
    }

    void OnUpdate() {
        // A REQUEST FROM ANOTHER SCRIPT IS SERVICED HERE, in this library,
        // where the statics are the real ones.
        //
        // IGNORED WHILE A TRANSITION IS RUNNING, which is not politeness: a
        // second fade-out would start a second coroutine writing the same
        // alpha, and the two would fight over it every frame. A double-clicked
        // button is the ordinary way to get there.
        if (m_hasPending) {
            m_hasPending = false;
            if (s_phase == Phase::Idle) {
                StartCoroutine(FadeOutAndLoad(m_pendingScene));
            } else {
                ENGINE_LOG_INFO(eng::Channels::kGame,
                                "GameController: already changing scene, ignoring '{}'",
                                m_pendingScene);
            }
        }

        m_transitioning = (s_phase != Phase::Idle);
        SizeOverlayToView();
        PushAlpha();
    }

private:
    enum class Phase { Idle, FadingOut, FadingIn };

    // THE THREE THAT OUTLIVE THE SCENE. Only this library's own code reads
    // them; see the header note.
    inline static GameController* s_instance = nullptr;
    inline static Phase           s_phase    = Phase::Idle;
    inline static float           s_alpha    = 0.0f;   // 0 clear, 1 black

    eng::EntityId m_overlay{};
    std::string   m_pendingScene;
    bool          m_hasPending    = false;
    bool          m_transitioning = false;

    // ---- the two halves of a transition -------------------------------------

    // Ends by asking for the scene, and NOTHING AFTER THAT LINE RUNS: the load
    // at the end of this frame destroys the entity this coroutine belongs to,
    // and a coroutine dies with its component. That is why the phase is set to
    // FadingIn BEFORE the request rather than after - the next controller is
    // the only thing left to read it.
    eng::Coroutine FadeOutAndLoad(std::string scenePath) {
        s_phase = Phase::FadingOut;

        const float seconds = fadeOutSeconds > 0.0f ? fadeOutSeconds : 0.0001f;
        while (s_alpha < 1.0f) {
            // UNSCALED, so a paused or slow-motion game still leaves the
            // screen at the speed the designer set.
            s_alpha += eng::Time::UnscaledDeltaTime() / seconds;
            if (s_alpha > 1.0f) { s_alpha = 1.0f; }
            PushAlpha();
            co_yield nullptr;          // next frame
        }
        s_alpha = 1.0f;
        PushAlpha();

        s_phase = Phase::FadingIn;

        ENGINE_LOG_INFO(eng::Channels::kGame, "GameController: opening {}", scenePath);

        // REQUEST, never Load. Load would unload this scene from inside this
        // coroutine, destroying the frame that is running. See Engine.h.
        eng::Engine::Get().RequestScene(scenePath);
    }

    eng::Coroutine FadeIn() {
        const float seconds = fadeInSeconds > 0.0f ? fadeInSeconds : 0.0001f;
        while (s_alpha > 0.0f) {
            s_alpha -= eng::Time::UnscaledDeltaTime() / seconds;
            if (s_alpha < 0.0f) { s_alpha = 0.0f; }
            PushAlpha();
            co_yield nullptr;
        }
        s_alpha = 0.0f;
        PushAlpha();
        s_phase = Phase::Idle;
    }

    // ---- the overlay ---------------------------------------------------------
    //
    //  BUILT IN CODE RATHER THAN PUT IN THE SCENE FILE, so that adding this
    //  controller to a level is one entity and not two - and so that nobody
    //  can save a level with the overlay accidentally left opaque.
    void BuildOverlay() {
        m_overlay = CreateEntity("FadeOverlay");

        eng::Entity* overlay = FindEntity(m_overlay);
        if (overlay == nullptr) {
            ENGINE_LOG_WARN(eng::Channels::kGame,
                            "GameController: could not create the fade overlay");
            return;
        }

        if (auto* rect = overlay->Add<eng::UIRectComponent>()) {
            rect->space     = eng::UISpace::Screen;
            rect->anchor    = eng::UIAnchor::Center;
            rect->pivot     = eng::Vec2{0.5f, 0.5f};
            rect->offset    = eng::Vec2{0.0f, 0.0f};
            rect->size      = eng::Vec2{4096.0f, 4096.0f};   // replaced every frame
            rect->sortOrder = 10000;                         // above every other UI
        }
        if (auto* image = overlay->Add<eng::UIImageComponent>()) {
            image->tint = eng::Color{0, 0, 0, 0};
        }
    }

    // ASKED FOR EVERY FRAME rather than once, because the view is resized by
    // dragging the editor's panels about as well as by the window.
    void SizeOverlayToView() const {
        eng::Entity* overlay = FindEntity(m_overlay);
        if (overlay == nullptr) {
            return;
        }
        auto* rect = overlay->Find<eng::UIRectComponent>();
        if (rect == nullptr) {
            return;
        }
        const eng::Vec2 view = eng::Pointer::ViewSize();
        if (view.x > 0.0f && view.y > 0.0f) {
            // A couple of pixels proud of the view, so no seam shows at the
            // edge when the size is an odd number.
            rect->size = eng::Vec2{view.x + 4.0f, view.y + 4.0f};
        }
    }

    // Looked up again on every call. Whole frames pass between two co_yields,
    // so a component pointer found before one may not be held across it.
    void PushAlpha() const {
        eng::Entity* overlay = FindEntity(m_overlay);
        if (overlay == nullptr) {
            return;
        }
        auto* image = overlay->Find<eng::UIImageComponent>();
        if (image == nullptr) {
            return;
        }
        float clamped = s_alpha;
        if (clamped < 0.0f) { clamped = 0.0f; }
        if (clamped > 1.0f) { clamped = 1.0f; }
        image->tint = eng::Color{0, 0, 0,
                                 static_cast<unsigned char>(clamped * 255.0f + 0.5f)};
    }
};


ENGINE_REGISTER_SCRIPT(GameController)
