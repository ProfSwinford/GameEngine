#pragma once

// ============================================================================
//  ScriptHooks.h - the machinery that works out which lifecycle functions your
//  script has. It is kept in its own file so that ScriptComponent.h - the one
//  you actually read to learn how scripts work - stays about scripts.
//
//  ==========================================================================
//  YOU DO NOT NEED TO READ THIS FILE TO WRITE A SCRIPT.
//
//  Everything here happens while your script is being compiled, and none of it
//  appears in code you write. If you only want to write a script, read
//  ScriptComponent.h and stop there.
//
//  What is in here is the answer to one question, asked by the compiler once
//  per script: "does this class have an OnUpdate?" That is the same question a
//  C# engine answers with reflection while the game runs - except this one
//  costs nothing at run time, because the answer is settled before the program
//  ever starts. It is the most advanced code in the engine, and it is the
//  reason you can write a script with no `virtual` and no `override`.
// ============================================================================

#include <engine/physics/Contact.h>
#include <engine/scene/EntityId.h>

#include <string>
#include <type_traits>

namespace eng {

// Declared, not defined: these functions only ever hold a POINTER to a script,
// and a pointer needs no more than the name of the type. ScriptComponent.h has
// the real class.
class ScriptBehaviour;

// ScriptFields comes from scene/ScriptFields.h, and a DECLARATION is all this
// file needs: the hook below only ever holds a POINTER to a function taking a
// reference to one. Keeping the full header out matters - it pulls in JSON and
// the renderer, and this file is included by every script in the project.
class ScriptFields;

// Collision2D comes from physics/Contact.h, which exists precisely so that
// this file can have the type and not the whole physics system. See the note
// at the top of that header for why a forward declaration will not do.


// ---------------------------------------------------------------------------
//  "Does this class have that function?", asked at compile time.
//
//  A `requires` expression is C++20's way of writing exactly that question.
//  `requires(T& t) { t.OnUpdate(); }` is a compile-time bool that is true when
//  `t.OnUpdate()` would compile and false when it would not - and asking costs
//  nothing at run time, because the answer is baked in.
//
//  This is the direct equivalent of type.GetMethod("Update") in C#, with the
//  work done by the compiler instead of by the program while it runs.
//
//  NONE OF THE HOOKS TAKE ARGUMENTS. A script that wants to know how much time
//  has passed asks Time::DeltaTime(), which gives the right answer for
//  whichever update it is called from. See core/Time.h.
// ---------------------------------------------------------------------------
namespace hooks {

template <class T> concept HasOnStart       = requires(T& t) { t.OnStart(); };
template <class T> concept HasOnUpdate      = requires(T& t) { t.OnUpdate(); };
template <class T> concept HasOnFixedUpdate = requires(T& t) { t.OnFixedUpdate(); };
template <class T> concept HasOnDestroy     = requires(T& t) { t.OnDestroy(); };

// ---- the variables a script wants in the Inspector ----------------------
//
// NOT A LIFECYCLE HOOK. Nothing happens when it runs except a list being
// filled in, and a script that has only this one does nothing at all - which
// is why it is left out of the "has no hooks" check at the bottom of the
// macro. See ScriptFields.h.
template <class T>
concept HasOnExpose = requires(T& t, ScriptFields& fields) { t.OnExpose(fields); };

// ---- collisions, which may be written either of two ways -----------------
//
// A collision hook can take just the other entity, or the whole Collision2D
// with the contact normal and how hard they hit:
//
//     void OnCollisionEnter(eng::EntityId other)             // simple
//     void OnCollisionEnter(const eng::Collision2D& hit)     // full
//
// BOTH ARE SUPPORTED, and that is not indecision. The simple form is all a
// pickup or a damage trigger needs, and making every script take a struct it
// ignores would be a worse default. The full form is what tells "I landed on
// something" from "I walked into a wall", which an entity id cannot answer.
//
// When a script writes the full form it wins - see MakeScriptHooks.
template <class T>
concept HasOnCollisionEnterId = requires(T& t, EntityId other) {
    t.OnCollisionEnter(other);
};
template <class T>
concept HasOnCollisionStayId = requires(T& t, EntityId other) {
    t.OnCollisionStay(other);
};
template <class T>
concept HasOnCollisionExitId = requires(T& t, EntityId other) {
    t.OnCollisionExit(other);
};

template <class T>
concept HasOnCollisionEnterInfo = requires(T& t, const Collision2D& hit) {
    t.OnCollisionEnter(hit);
};
template <class T>
concept HasOnCollisionStayInfo = requires(T& t, const Collision2D& hit) {
    t.OnCollisionStay(hit);
};
template <class T>
concept HasOnCollisionExitInfo = requires(T& t, const Collision2D& hit) {
    t.OnCollisionExit(hit);
};

template <class T>
concept HasOnCollisionEnter = HasOnCollisionEnterId<T> || HasOnCollisionEnterInfo<T>;
template <class T>
concept HasOnCollisionStay = HasOnCollisionStayId<T> || HasOnCollisionStayInfo<T>;
template <class T>
concept HasOnCollisionExit = HasOnCollisionExitId<T> || HasOnCollisionExitInfo<T>;

// ---- triggers, which are a different event ------------------------------
//
// A pair where EITHER side is a trigger sends these and NOT the collision
// hooks above. See the note in physics/Collider.h: a trigger reports and
// nothing else, a solid collider is pushed apart, and they are different
// enough that one event for both would be a lie.
//
// These take an EntityId only. Unity hands over a Collider2D, but a pointer
// to a component that may already have been destroyed is exactly what this
// engine's collision events have always refused to hand out - look it up
// through the scene instead.
template <class T>
concept HasOnTriggerEnter = requires(T& t, EntityId other) { t.OnTriggerEnter(other); };
template <class T>
concept HasOnTriggerStay = requires(T& t, EntityId other) { t.OnTriggerStay(other); };
template <class T>
concept HasOnTriggerExit = requires(T& t, EntityId other) { t.OnTriggerExit(other); };

// "There is a member with this name", regardless of what it takes. Used only
// to produce a good error message: a class that HAS OnUpdate but whose
// OnUpdate cannot be called with no arguments has a wrong signature, not a
// missing hook, and saying so is far more useful than silently ignoring it.
template <class T> concept NamesOnStart       = requires { &T::OnStart; };
template <class T> concept NamesOnUpdate      = requires { &T::OnUpdate; };
template <class T> concept NamesOnFixedUpdate = requires { &T::OnFixedUpdate; };
template <class T> concept NamesOnDestroy     = requires { &T::OnDestroy; };
template <class T> concept NamesOnExpose      = requires { &T::OnExpose; };
template <class T> concept NamesOnCollisionEnter = requires { &T::OnCollisionEnter; };
template <class T> concept NamesOnCollisionStay  = requires { &T::OnCollisionStay; };
template <class T> concept NamesOnCollisionExit  = requires { &T::OnCollisionExit; };
template <class T> concept NamesOnTriggerEnter   = requires { &T::OnTriggerEnter; };
template <class T> concept NamesOnTriggerStay    = requires { &T::OnTriggerStay; };
template <class T> concept NamesOnTriggerExit    = requires { &T::OnTriggerExit; };

// THE OLD SIGNATURE, kept solely so that meeting it is an error with an
// explanation rather than a silent change of meaning.
//
// OnUpdate used to take the fixed step as an argument and run once per
// simulation step. It now takes nothing and runs once per DRAWN FRAME, which
// is a different rate on every machine. A script written against the old one
// would still have compiled if the argument had been kept - and would have
// quietly started running at the wrong rate, with no diagnostic anywhere.
// That is the single worst outcome available, so the argument was removed and
// this concept turns the old spelling into a compile error that says where
// the code should go instead.
template <class T>
concept TakesDeltaOnUpdate = requires(T& t, float dt) { t.OnUpdate(dt); };
template <class T>
concept TakesDeltaOnFixedUpdate = requires(T& t, float dt) { t.OnFixedUpdate(dt); };

// The hook names other engines use, so that using one by habit is a compile
// error naming the right one rather than a function that never runs.
//
// THESE HAVE TO BE TEMPLATES, and that is not a style choice. A requires
// expression only turns a failure into `false` when the failure happens while
// substituting a TEMPLATE PARAMETER. Written inline against a concrete class -
// `requires { &Bouncer::Update; }` - a missing member is simply ill-formed and
// the compiler reports it, which would make this check fail for every script
// that had done nothing wrong.
template <class T> concept NamesUpdate      = requires { &T::Update; };
template <class T> concept NamesFixedUpdate = requires { &T::FixedUpdate; };
template <class T> concept NamesLateUpdate  = requires { &T::LateUpdate; };
template <class T> concept NamesStart       = requires { &T::Start; };
template <class T> concept NamesAwake       = requires { &T::Awake; };

// The spellings somebody reaches for instead of OnExpose. Same reasoning as
// the block above: a function nobody calls is worse than a compile error, and
// a variable list that is never asked for leaves the Inspector empty with
// nothing anywhere saying why.
template <class T> concept NamesExpose       = requires { &T::Expose; };
template <class T> concept NamesExposeFields = requires { &T::OnExposeFields; };
template <class T> concept NamesExposeVars   = requires { &T::OnExposeVars; };
template <class T> concept NamesInspect      = requires { &T::OnInspect; };
template <class T> concept NamesInspector    = requires { &T::OnInspectorGUI; };
template <class T> concept NamesValidate     = requires { &T::OnValidate; };
template <class T> concept NamesSerialize    = requires { &T::OnSerialize; };

// Unity suffixes its 2D physics callbacks with "2D", which is the single
// easiest habit to carry over - and the resulting function is one nobody
// calls. This whole engine is 2D, so there is nothing to tell apart.
template <class T> concept NamesCollisionEnter2D = requires { &T::OnCollisionEnter2D; };
template <class T> concept NamesCollisionStay2D  = requires { &T::OnCollisionStay2D; };
template <class T> concept NamesCollisionExit2D  = requires { &T::OnCollisionExit2D; };
template <class T> concept NamesTriggerEnter2D   = requires { &T::OnTriggerEnter2D; };
template <class T> concept NamesTriggerStay2D    = requires { &T::OnTriggerStay2D; };
template <class T> concept NamesTriggerExit2D    = requires { &T::OnTriggerExit2D; };

} // namespace hooks

// ---------------------------------------------------------------------------
//  The hooks one script type turned out to have.
//
//  Plain function pointers, not std::function: there is nothing to capture and
//  nothing to allocate. A null pointer means "this script did not write that
//  hook", and every caller checks - which is what makes an absent hook free
//  rather than cheap.
// ---------------------------------------------------------------------------
struct ScriptHooks {
    void (*start)(ScriptBehaviour*)       = nullptr;
    void (*update)(ScriptBehaviour*)      = nullptr;   // once per drawn frame
    void (*fixedUpdate)(ScriptBehaviour*) = nullptr;   // once per simulation step
    void (*destroy)(ScriptBehaviour*)     = nullptr;

    // Fills in the list of variables the Inspector shows. Called once, when
    // the script is bound - NOT every frame, and not when anything changes.
    // A null pointer means the script exposes nothing, which is most of them.
    void (*expose)(ScriptBehaviour*, ScriptFields&) = nullptr;

    // ONE POINTER PER EVENT, taking the full Collision2D, whichever way the
    // script wrote its hook. A script that only asked for the entity gets a
    // lambda that pulls it out of the struct - so this table does not need two
    // slots per event and the dispatcher does not need to choose between them.
    void (*collisionEnter)(ScriptBehaviour*, const Collision2D&) = nullptr;
    void (*collisionStay)(ScriptBehaviour*, const Collision2D&)  = nullptr;
    void (*collisionExit)(ScriptBehaviour*, const Collision2D&)  = nullptr;

    void (*triggerEnter)(ScriptBehaviour*, EntityId) = nullptr;
    void (*triggerStay)(ScriptBehaviour*, EntityId)  = nullptr;
    void (*triggerExit)(ScriptBehaviour*, EntityId)  = nullptr;

    bool AnyCollision() const {
        return collisionEnter != nullptr || collisionStay != nullptr ||
               collisionExit != nullptr;
    }
    bool AnyTrigger() const {
        return triggerEnter != nullptr || triggerStay != nullptr ||
               triggerExit != nullptr;
    }
    bool AnyContact() const { return AnyCollision() || AnyTrigger(); }
};

// Builds the table for one script type.
//
// Each entry is a capture-less lambda, which C++ converts to an ordinary
// function pointer. Inside it, the object is cast back to the real type and
// the hook called directly - so the compiler can inline the whole thing. The
// `if constexpr` is what makes an unwritten hook cost nothing: the branch does
// not merely evaluate false, it is not compiled at all.
template <class T>
constexpr ScriptHooks MakeScriptHooks() {
    ScriptHooks h;
    if constexpr (hooks::HasOnStart<T>) {
        h.start = [](ScriptBehaviour* s) { static_cast<T*>(s)->OnStart(); };
    }
    if constexpr (hooks::HasOnUpdate<T>) {
        h.update = [](ScriptBehaviour* s) { static_cast<T*>(s)->OnUpdate(); };
    }
    if constexpr (hooks::HasOnFixedUpdate<T>) {
        h.fixedUpdate = [](ScriptBehaviour* s) { static_cast<T*>(s)->OnFixedUpdate(); };
    }
    if constexpr (hooks::HasOnDestroy<T>) {
        h.destroy = [](ScriptBehaviour* s) { static_cast<T*>(s)->OnDestroy(); };
    }
    if constexpr (hooks::HasOnExpose<T>) {
        h.expose = [](ScriptBehaviour* s, ScriptFields& f) {
            static_cast<T*>(s)->OnExpose(f);
        };
    }
    // THE FULL FORM WINS when a script wrote both, because a script that went
    // to the trouble of asking for the contact data wants the contact data.
    // `.other` is how the simple form is served from the same pointer.
    if constexpr (hooks::HasOnCollisionEnterInfo<T>) {
        h.collisionEnter = [](ScriptBehaviour* s, const Collision2D& c) {
            static_cast<T*>(s)->OnCollisionEnter(c);
        };
    } else if constexpr (hooks::HasOnCollisionEnterId<T>) {
        h.collisionEnter = [](ScriptBehaviour* s, const Collision2D& c) {
            static_cast<T*>(s)->OnCollisionEnter(c.other);
        };
    }

    if constexpr (hooks::HasOnCollisionStayInfo<T>) {
        h.collisionStay = [](ScriptBehaviour* s, const Collision2D& c) {
            static_cast<T*>(s)->OnCollisionStay(c);
        };
    } else if constexpr (hooks::HasOnCollisionStayId<T>) {
        h.collisionStay = [](ScriptBehaviour* s, const Collision2D& c) {
            static_cast<T*>(s)->OnCollisionStay(c.other);
        };
    }

    if constexpr (hooks::HasOnCollisionExitInfo<T>) {
        h.collisionExit = [](ScriptBehaviour* s, const Collision2D& c) {
            static_cast<T*>(s)->OnCollisionExit(c);
        };
    } else if constexpr (hooks::HasOnCollisionExitId<T>) {
        h.collisionExit = [](ScriptBehaviour* s, const Collision2D& c) {
            static_cast<T*>(s)->OnCollisionExit(c.other);
        };
    }

    if constexpr (hooks::HasOnTriggerEnter<T>) {
        h.triggerEnter = [](ScriptBehaviour* s, EntityId o) {
            static_cast<T*>(s)->OnTriggerEnter(o);
        };
    }
    if constexpr (hooks::HasOnTriggerStay<T>) {
        h.triggerStay = [](ScriptBehaviour* s, EntityId o) {
            static_cast<T*>(s)->OnTriggerStay(o);
        };
    }
    if constexpr (hooks::HasOnTriggerExit<T>) {
        h.triggerExit = [](ScriptBehaviour* s, EntityId o) {
            static_cast<T*>(s)->OnTriggerExit(o);
        };
    }
    return h;
}

// "OnStart, OnFixedUpdate" - for the Console line that lists what each script
// has.
std::string DescribeHooks(const ScriptHooks& hooks);

} // namespace eng

// ----------------------------------------------------------------------------
//  ENGINE_REGISTER_SCRIPT(MyScript) - put this at the bottom of your file.
//
//  It does two jobs. It works out which lifecycle hooks your class has, and it
//  adds the class to the registry under its own name.
//
//  The registration happens through one small object at file scope, whose
//  constructor runs automatically when the library is loaded - which is how
//  the engine learns your script's name without anybody editing a shared list.
//
//  The ## in Type##_Registrar is the preprocessor's "glue these together"
//  operator, so ENGINE_REGISTER_SCRIPT(Bouncer) produces a class called
//  Bouncer_Registrar. The #Type turns the name into the text "Bouncer".
//
//  THE static_asserts ARE THE POINT OF HALF THIS MACRO. Because hooks are
//  found by name rather than declared, a mistyped one would otherwise just be
//  ignored. These turn the three mistakes that are actually worth catching -
//  right name with the wrong arguments, the name from a different engine, and
//  the OnUpdate(float) this engine itself used to have - into compile errors
//  that say what to write instead.
// ----------------------------------------------------------------------------
#define ENGINE_REGISTER_SCRIPT(Type)                                                   \
    /* --- is this even a script? ------------------------------------------ */       \
    static_assert(std::is_base_of_v<::eng::ScriptBehaviour, Type>,                     \
                  #Type " must inherit from eng::ScriptBehaviour: "                    \
                        "class " #Type " : public eng::ScriptBehaviour { ... };");     \
    static_assert(std::is_default_constructible_v<Type>,                               \
                  #Type " needs a constructor that takes no arguments, because the "   \
                        "engine creates it for you when a scene loads.");              \
    /* --- the hook that changed, caught before it changes meaning ---------- */       \
    /* Deliberately FIRST: a script written against the old engine hits this, */       \
    /* and a general message about signatures would be no help at all.        */       \
    static_assert(!::eng::hooks::TakesDeltaOnUpdate<Type>,                             \
                  #Type "::OnUpdate(float) is gone, and its replacement depends on "   \
                        "what the code does. There are now TWO updates, running at "   \
                        "DIFFERENT RATES, and neither takes an argument: "             \
                        "void OnFixedUpdate() runs every fixed simulation step and "   \
                        "is where movement, physics and anything timed belongs - it "  \
                        "is what OnUpdate(float) used to be, so start by renaming it " \
                        "to that. void OnUpdate() runs once per DRAWN FRAME, which "   \
                        "is a different rate on every machine, and is for things "     \
                        "that follow the camera or the screen. For the delta, ask "    \
                        "eng::Time::DeltaTime() - it returns the right one for "       \
                        "whichever hook you are in. See engine/core/Time.h.");         \
    static_assert(!::eng::hooks::TakesDeltaOnFixedUpdate<Type>,                        \
                  #Type "::OnFixedUpdate takes no argument: void OnFixedUpdate(). "    \
                        "Ask eng::Time::FixedDeltaTime() for the step size.");         \
    /* --- right name, wrong signature -------------------------------------- */      \
    static_assert(!(::eng::hooks::NamesOnUpdate<Type> &&                               \
                    !::eng::hooks::HasOnUpdate<Type> &&                                \
                    !::eng::hooks::TakesDeltaOnUpdate<Type>),                          \
                  #Type "::OnUpdate has the wrong signature. It must be: "             \
                        "void OnUpdate()");                                            \
    static_assert(!(::eng::hooks::NamesOnFixedUpdate<Type> &&                          \
                    !::eng::hooks::HasOnFixedUpdate<Type> &&                           \
                    !::eng::hooks::TakesDeltaOnFixedUpdate<Type>),                     \
                  #Type "::OnFixedUpdate has the wrong signature. It must be: "        \
                        "void OnFixedUpdate()");                                       \
    static_assert(!(::eng::hooks::NamesOnStart<Type> && !::eng::hooks::HasOnStart<Type>),\
                  #Type "::OnStart has the wrong signature. It must be: "              \
                        "void OnStart()");                                             \
    static_assert(!(::eng::hooks::NamesOnDestroy<Type> &&                              \
                    !::eng::hooks::HasOnDestroy<Type>),                                \
                  #Type "::OnDestroy has the wrong signature. It must be: "            \
                        "void OnDestroy()");                                           \
    static_assert(!(::eng::hooks::NamesOnExpose<Type> &&                               \
                    !::eng::hooks::HasOnExpose<Type>),                                 \
                  #Type "::OnExpose has the wrong signature. It must be: "             \
                        "void OnExpose(eng::ScriptFields& fields) - taking a "          \
                        "NON-CONST reference, because the function fills it in. "      \
                        "See engine/scene/ScriptFields.h.");                           \
    static_assert(!(::eng::hooks::NamesOnCollisionEnter<Type> &&                       \
                    !::eng::hooks::HasOnCollisionEnter<Type>),                         \
                  #Type "::OnCollisionEnter has the wrong signature. It must take "    \
                        "EITHER the other entity or the whole contact: "               \
                        "void OnCollisionEnter(eng::EntityId other) or "               \
                        "void OnCollisionEnter(const eng::Collision2D& hit)");         \
    static_assert(!(::eng::hooks::NamesOnCollisionStay<Type> &&                        \
                    !::eng::hooks::HasOnCollisionStay<Type>),                          \
                  #Type "::OnCollisionStay has the wrong signature. It must be: "      \
                        "void OnCollisionStay(eng::EntityId other) or "                \
                        "void OnCollisionStay(const eng::Collision2D& hit)");          \
    static_assert(!(::eng::hooks::NamesOnCollisionExit<Type> &&                        \
                    !::eng::hooks::HasOnCollisionExit<Type>),                          \
                  #Type "::OnCollisionExit has the wrong signature. It must be: "      \
                        "void OnCollisionExit(eng::EntityId other) or "                \
                        "void OnCollisionExit(const eng::Collision2D& hit)");          \
    static_assert(!(::eng::hooks::NamesOnTriggerEnter<Type> &&                         \
                    !::eng::hooks::HasOnTriggerEnter<Type>),                           \
                  #Type "::OnTriggerEnter has the wrong signature. It must be: "       \
                        "void OnTriggerEnter(eng::EntityId other)");                   \
    static_assert(!(::eng::hooks::NamesOnTriggerStay<Type> &&                          \
                    !::eng::hooks::HasOnTriggerStay<Type>),                            \
                  #Type "::OnTriggerStay has the wrong signature. It must be: "        \
                        "void OnTriggerStay(eng::EntityId other)");                    \
    static_assert(!(::eng::hooks::NamesOnTriggerExit<Type> &&                          \
                    !::eng::hooks::HasOnTriggerExit<Type>),                            \
                  #Type "::OnTriggerExit has the wrong signature. It must be: "        \
                        "void OnTriggerExit(eng::EntityId other)");                    \
    /* --- the names other engines use --------------------------------------- */     \
    static_assert(!::eng::hooks::NamesUpdate<Type>,                                    \
                  "This engine calls it OnUpdate(), not Update. Rename " #Type         \
                  "::Update to OnUpdate - and check whether it should be "             \
                  "OnFixedUpdate() instead, which is the one for physics.");           \
    static_assert(!::eng::hooks::NamesFixedUpdate<Type>,                               \
                  "This engine calls it OnFixedUpdate(), not FixedUpdate. Rename "     \
                  #Type "::FixedUpdate to OnFixedUpdate, and drop the argument if "    \
                  "it has one - ask eng::Time::FixedDeltaTime() instead.");            \
    static_assert(!::eng::hooks::NamesLateUpdate<Type>,                                \
                  "This engine has no LateUpdate. The usual reason for one is a "      \
                  "camera that must move after whatever it follows, and that is "      \
                  "already handled: CameraSystem runs at stage 700, after all "        \
                  "gameplay and movement. See scene/SystemOrder.h.");                  \
    static_assert(!::eng::hooks::NamesStart<Type>,                                     \
                  "This engine calls it OnStart(), not Start. Rename " #Type           \
                  "::Start to OnStart.");                                              \
    static_assert(!::eng::hooks::NamesAwake<Type>,                                     \
                  "This engine has no Awake. The first hook is OnStart(), which runs " \
                  "on the first update after the script is attached.");                \
    /* --- the spellings reached for instead of OnExpose -------------------- */       \
    /* An unasked-for variable list is invisible: the Inspector simply shows  */       \
    /* nothing, which looks exactly like a script that exposes nothing.       */       \
    static_assert(!(::eng::hooks::NamesExpose<Type> ||                                 \
                    ::eng::hooks::NamesExposeFields<Type> ||                           \
                    ::eng::hooks::NamesExposeVars<Type>),                              \
                  "This engine calls it OnExpose(eng::ScriptFields&). Rename it to "   \
                  "that - every function the engine calls starts with On, and "        \
                  "nothing would ever ask for this list under another name.");         \
    static_assert(!(::eng::hooks::NamesInspect<Type> ||                                \
                    ::eng::hooks::NamesInspector<Type>),                               \
                  "This engine has no OnInspect / OnInspectorGUI. A script does not "  \
                  "draw its own Inspector - it LISTS its variables and the editor "    \
                  "draws them: void OnExpose(eng::ScriptFields& fields) { "            \
                  "fields.Var(\"speed\", speed); }");                                  \
    static_assert(!::eng::hooks::NamesValidate<Type>,                                  \
                  "This engine has no OnValidate. Nothing is called when a value is "  \
                  "changed in the Inspector - a script reads its variables when it "   \
                  "needs them, in OnUpdate or OnFixedUpdate, so a change takes "       \
                  "effect on the next one with nothing to notify.");                   \
    static_assert(!::eng::hooks::NamesSerialize<Type>,                                 \
                  "This engine has no OnSerialize. A script does not write itself "    \
                  "into the scene file; it lists its variables with "                  \
                  "void OnExpose(eng::ScriptFields& fields) and the engine saves "      \
                  "whichever of them were changed. See scene/ScriptFields.h.");        \
    /* --- Unity's "2D" suffix, which there is nothing here to tell apart --- */       \
    static_assert(!(::eng::hooks::NamesCollisionEnter2D<Type> ||                       \
                    ::eng::hooks::NamesCollisionStay2D<Type> ||                        \
                    ::eng::hooks::NamesCollisionExit2D<Type>),                         \
                  "Drop the \"2D\": this engine calls it OnCollisionEnter / Stay / "   \
                  "Exit. The whole engine is 2D, so there is no 3D version to tell "   \
                  "it apart from.");                                                   \
    static_assert(!(::eng::hooks::NamesTriggerEnter2D<Type> ||                         \
                    ::eng::hooks::NamesTriggerStay2D<Type> ||                          \
                    ::eng::hooks::NamesTriggerExit2D<Type>),                           \
                  "Drop the \"2D\": this engine calls it OnTriggerEnter / Stay / "     \
                  "Exit.");                                                            \
    /* --- the catch-all, deliberately LAST ---------------------------------- */      \
    /* Every check above names one specific mistake, and a specific message is */      \
    /* worth more than a general one - so this general one is the last to fire. */     \
    /* The two old signatures are listed as well, so that a script hitting the  */     \
    /* migration assert above is told THAT and not also told it has no hooks,   */     \
    /* which would flatly contradict it.                                        */     \
    static_assert(::eng::hooks::HasOnStart<Type> || ::eng::hooks::HasOnUpdate<Type> || \
                      ::eng::hooks::HasOnFixedUpdate<Type> ||                          \
                      ::eng::hooks::TakesDeltaOnUpdate<Type> ||                        \
                      ::eng::hooks::TakesDeltaOnFixedUpdate<Type> ||                   \
                      ::eng::hooks::HasOnDestroy<Type> ||                              \
                      ::eng::hooks::HasOnCollisionEnter<Type> ||                       \
                      ::eng::hooks::HasOnCollisionStay<Type> ||                        \
                      ::eng::hooks::HasOnCollisionExit<Type> ||                        \
                      ::eng::hooks::HasOnTriggerEnter<Type> ||                         \
                      ::eng::hooks::HasOnTriggerStay<Type> ||                          \
                      ::eng::hooks::HasOnTriggerExit<Type>,                            \
                  #Type " has none of the lifecycle functions, so nothing would ever " \
                        "call it. OnExpose does not count - it only lists variables "  \
                        "for the Inspector and never runs anything, so a script needs "\
                        "one of the real hooks as well. Two things to check: the "     \
                        "SPELLING (OnStart, "                                          \
                        "OnUpdate, OnFixedUpdate, OnDestroy, "                         \
                        "OnCollisionEnter/Stay/Exit, OnTriggerEnter/Stay/Exit), and "  \
                        "that they are PUBLIC - the engine calls them from outside "   \
                        "the class, so a private one cannot be seen.");                \
    namespace {                                                                        \
    struct Type##_Registrar {                                                          \
        Type##_Registrar() {                                                           \
            ::eng::ScriptRegistry::Register(                                           \
                #Type,                                                                 \
                []() -> std::unique_ptr<::eng::ScriptBehaviour> {                      \
                    return std::make_unique<Type>();                                   \
                },                                                                     \
                ::eng::MakeScriptHooks<Type>(),                                        \
                __FILE__);                                                             \
        }                                                                              \
    };                                                                                 \
    const Type##_Registrar g_##Type##_registrar;                                       \
    }
