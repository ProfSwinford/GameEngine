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

#include <engine/scene/EntityId.h>

#include <string>
#include <type_traits>

namespace eng {

// Declared, not defined: these functions only ever hold a POINTER to a script,
// and a pointer needs no more than the name of the type. ScriptComponent.h has
// the real class.
class ScriptBehaviour;

// ---------------------------------------------------------------------------
//  "Does this class have that function?", asked at compile time.
//
//  A `requires` expression is C++20's way of writing exactly that question.
//  `requires(T& t, float dt) { t.OnUpdate(dt); }` is a compile-time bool that
//  is true when `t.OnUpdate(dt)` would compile and false when it would not -
//  and asking costs nothing at run time, because the answer is baked in.
//
//  This is the direct equivalent of type.GetMethod("Update") in C#, with the
//  work done by the compiler instead of by the program while it runs.
// ---------------------------------------------------------------------------
namespace hooks {

template <class T> concept HasOnStart   = requires(T& t) { t.OnStart(); };
template <class T> concept HasOnUpdate  = requires(T& t, float dt) { t.OnUpdate(dt); };
template <class T> concept HasOnDestroy = requires(T& t) { t.OnDestroy(); };

template <class T>
concept HasOnCollisionEnter = requires(T& t, EntityId other) { t.OnCollisionEnter(other); };
template <class T>
concept HasOnCollisionStay = requires(T& t, EntityId other) { t.OnCollisionStay(other); };
template <class T>
concept HasOnCollisionExit = requires(T& t, EntityId other) { t.OnCollisionExit(other); };

// "There is a member with this name", regardless of what it takes. Used only
// to produce a good error message: a class that HAS OnUpdate but whose
// OnUpdate cannot be called with a float has a wrong signature, not a missing
// hook, and saying so is far more useful than silently ignoring it.
template <class T> concept NamesOnStart   = requires { &T::OnStart; };
template <class T> concept NamesOnUpdate  = requires { &T::OnUpdate; };
template <class T> concept NamesOnDestroy = requires { &T::OnDestroy; };
template <class T> concept NamesOnCollisionEnter = requires { &T::OnCollisionEnter; };
template <class T> concept NamesOnCollisionStay  = requires { &T::OnCollisionStay; };
template <class T> concept NamesOnCollisionExit  = requires { &T::OnCollisionExit; };

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
template <class T> concept NamesStart       = requires { &T::Start; };
template <class T> concept NamesAwake       = requires { &T::Awake; };

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
    void (*start)(ScriptBehaviour*)                  = nullptr;
    void (*update)(ScriptBehaviour*, float)          = nullptr;
    void (*destroy)(ScriptBehaviour*)                = nullptr;
    void (*collisionEnter)(ScriptBehaviour*, EntityId) = nullptr;
    void (*collisionStay)(ScriptBehaviour*, EntityId)  = nullptr;
    void (*collisionExit)(ScriptBehaviour*, EntityId)  = nullptr;

    bool AnyCollision() const {
        return collisionEnter != nullptr || collisionStay != nullptr ||
               collisionExit != nullptr;
    }
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
        h.update = [](ScriptBehaviour* s, float dt) { static_cast<T*>(s)->OnUpdate(dt); };
    }
    if constexpr (hooks::HasOnDestroy<T>) {
        h.destroy = [](ScriptBehaviour* s) { static_cast<T*>(s)->OnDestroy(); };
    }
    if constexpr (hooks::HasOnCollisionEnter<T>) {
        h.collisionEnter = [](ScriptBehaviour* s, EntityId o) {
            static_cast<T*>(s)->OnCollisionEnter(o);
        };
    }
    if constexpr (hooks::HasOnCollisionStay<T>) {
        h.collisionStay = [](ScriptBehaviour* s, EntityId o) {
            static_cast<T*>(s)->OnCollisionStay(o);
        };
    }
    if constexpr (hooks::HasOnCollisionExit<T>) {
        h.collisionExit = [](ScriptBehaviour* s, EntityId o) {
            static_cast<T*>(s)->OnCollisionExit(o);
        };
    }
    return h;
}

// "OnStart, OnUpdate" - for the Console line that lists what each script has.
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
//  ignored. These turn the two mistakes that are actually worth catching -
//  right name with the wrong arguments, and the name from a different engine -
//  into compile errors that say what to write instead.
// ----------------------------------------------------------------------------
#define ENGINE_REGISTER_SCRIPT(Type)                                                   \
    /* --- is this even a script? ------------------------------------------ */        \
    static_assert(std::is_base_of_v<::eng::ScriptBehaviour, Type>,                     \
                  #Type " must inherit from eng::ScriptBehaviour: "                     \
                        "class " #Type " : public eng::ScriptBehaviour { ... };");      \
    static_assert(std::is_default_constructible_v<Type>,                               \
                  #Type " needs a constructor that takes no arguments, because the "    \
                        "engine creates it for you when a scene loads.");               \
    /* --- right name, wrong signature -------------------------------------- */       \
    static_assert(!(::eng::hooks::NamesOnUpdate<Type> &&                                \
                    !::eng::hooks::HasOnUpdate<Type>),                                  \
                  #Type "::OnUpdate has the wrong signature. It must be: "              \
                        "void OnUpdate(float deltaSeconds)");                           \
    static_assert(!(::eng::hooks::NamesOnStart<Type> && !::eng::hooks::HasOnStart<Type>),\
                  #Type "::OnStart has the wrong signature. It must be: "               \
                        "void OnStart()");                                              \
    static_assert(!(::eng::hooks::NamesOnDestroy<Type> &&                               \
                    !::eng::hooks::HasOnDestroy<Type>),                                 \
                  #Type "::OnDestroy has the wrong signature. It must be: "             \
                        "void OnDestroy()");                                            \
    static_assert(!(::eng::hooks::NamesOnCollisionEnter<Type> &&                        \
                    !::eng::hooks::HasOnCollisionEnter<Type>),                          \
                  #Type "::OnCollisionEnter has the wrong signature. It must be: "      \
                        "void OnCollisionEnter(eng::EntityId other)");                  \
    static_assert(!(::eng::hooks::NamesOnCollisionStay<Type> &&                         \
                    !::eng::hooks::HasOnCollisionStay<Type>),                           \
                  #Type "::OnCollisionStay has the wrong signature. It must be: "       \
                        "void OnCollisionStay(eng::EntityId other)");                   \
    static_assert(!(::eng::hooks::NamesOnCollisionExit<Type> &&                         \
                    !::eng::hooks::HasOnCollisionExit<Type>),                           \
                  #Type "::OnCollisionExit has the wrong signature. It must be: "       \
                        "void OnCollisionExit(eng::EntityId other)");                   \
    /* --- the names other engines use --------------------------------------- */      \
    static_assert(!::eng::hooks::NamesUpdate<Type>,                                     \
                  "This engine calls it OnUpdate(float), not Update. Rename " #Type     \
                  "::Update to OnUpdate.");                                             \
    static_assert(!::eng::hooks::NamesFixedUpdate<Type>,                                \
                  "This engine has one update, and it is already fixed-rate. Rename "   \
                  #Type "::FixedUpdate to OnUpdate(float).");                           \
    static_assert(!::eng::hooks::NamesStart<Type>,                                      \
                  "This engine calls it OnStart(), not Start. Rename " #Type            \
                  "::Start to OnStart.");                                               \
    static_assert(!::eng::hooks::NamesAwake<Type>,                                      \
                  "This engine has no Awake. The first hook is OnStart(), which runs "  \
                  "on the first simulation step after the script is attached.");        \
    /* --- the catch-all, deliberately LAST ---------------------------------- */       \
    /* Every check above names one specific mistake, and a specific message is */       \
    /* worth more than a general one - so this general one is the last to fire. */      \
    static_assert(::eng::hooks::HasOnStart<Type> || ::eng::hooks::HasOnUpdate<Type> ||  \
                      ::eng::hooks::HasOnDestroy<Type> ||                               \
                      ::eng::hooks::HasOnCollisionEnter<Type> ||                        \
                      ::eng::hooks::HasOnCollisionStay<Type> ||                         \
                      ::eng::hooks::HasOnCollisionExit<Type>,                           \
                  #Type " has none of the lifecycle functions, so nothing would ever "  \
                        "call it. Two things to check: the SPELLING (OnStart, "         \
                        "OnUpdate, OnDestroy, OnCollisionEnter/Stay/Exit), and that "   \
                        "they are PUBLIC - the engine calls them from outside the "     \
                        "class, so a private one cannot be seen.");                     \
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


