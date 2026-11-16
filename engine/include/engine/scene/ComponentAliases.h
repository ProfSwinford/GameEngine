#pragma once

// ============================================================================
//  ComponentAliases.h - what makes an engine component read exactly like a
//  script the game wrote.
//
//  ==========================================================================
//  THE RULE, IN ONE LINE
//
//  Inside a script, a BARE component name is a REFERENCE to that component.
//  The QUALIFIED name is the class.
//
//      SpriteComponent renderer;        // a slot you drag an entity onto
//      eng::SpriteComponent             // the class itself
//
//  That is exactly the rule a script already followed - `Invisibility invis;`
//  is a reference and the class behind it is something else - and this header
//  extends it to the components the engine ships, so there is one rule rather
//  than two.
//
//  ==========================================================================
//  WHY THESE ARE MEMBERS OF ScriptBehaviour, AND NOT A NAMESPACE
//
//  Because a script writes `using namespace eng;`, and that makes eng's names
//  appear as if they were declared in the GLOBAL namespace. So an alias at
//  global scope, or in any namespace reached by a second using-directive, is
//  a second `SpriteComponent` in the same scope - and every use of the name
//  is then ambiguous:
//
//      error C2872: 'SpriteComponent': ambiguous symbol
//
//  A member of the class hides both. Unqualified lookup inside a class
//  searches that class and its bases BEFORE the enclosing namespace, so an
//  alias inherited from ScriptBehaviour wins outright, with no ambiguity and
//  nothing to order. Every script derives from it, so every script has them.
//
//  AND THAT IS WHY YOUR IDE AGREES. This is an ordinary header, so the rule
//  holds in the file you are editing exactly as it holds in the unit the
//  editor generates - there is no generated scope for an IDE to be ignorant
//  of. An earlier version of this put the aliases in the generated namespace
//  instead; it compiled, and Visual Studio underlined every line that used
//  one, because the file on its own said something different from the file
//  the compiler saw. One mechanism in one header is the fix for that.
//
//  ==========================================================================
//  WHAT THIS DOES NOT COVER, and why each one is out
//
//    AT FILE SCOPE the bare name is still the class, because there is no
//    class scope to look in. That is the right answer: a free function taking
//    a `SpriteComponent*` means the component, and a field means a reference.
//
//    eng::ColliderComponent has no kTypeName - it is the abstract base of the
//    concrete colliders - so ComponentRef cannot resolve it, and the
//    static_assert in ComponentRef says so. `Collider2D` stays a plain
//    pointer; eng::FindCollider(Entity&) is the hand-written answer.
//
//    eng::ScriptComponent is deliberately absent. A reference to "whichever
//    script happens to be on that entity" is not a useful question - asking
//    it finds an arbitrary one - and the spelling for a reference to a
//    PARTICULAR script is that script's own bare name, which is the thing
//    this whole header is modelled on. ScriptBehaviour also names the class
//    in its own body, which an alias of the same name would break.
//
//    Transform2D is not a component at all. It is the value a
//    TransformComponent holds, which is why a reference to the component
//    resolves one step further - see ComponentRefTarget.
//
//  ADDING A COMPONENT? THERE ARE FIVE PLACES, not two, and three of them
//  fail silently:
//
//    1. scene/ScriptComponent.h, the forward-declaration block. Without it
//       the alias below names no type.
//    2. HERE, which makes the bare name compile as a reference.
//    3. Engine.h, the Unity-style short typedef (SpriteRenderer, PlayerInput).
//    4. engine/src/tools/ScriptVars.cpp, kTypeSpellings - TWO entries, one in
//       each half of the array - which tells the Inspector which entities may
//       be dropped on the slot.
//    5. tests/src/test_main.cpp, the RegisterComponentTypes list, which must
//       mirror Engine::SceneSubsystem::Init in the same order. Miss it and
//       Entity::Add<T>() returns null in every test that builds the component
//       by name, which reads as a null-pointer bug in brand-new test code.
//
//  ONLY 2 AND 4 ARE TESTED, by the paired cases in test_scriptvars.cpp - and
//  those are hand-written per-component lists rather than loops over a
//  registry, so they do not fail on their own when a component is added and
//  forgotten. They catch the DRIFT once a name is in one list and not the
//  other, which is a narrower promise than it first reads as.
//
//  AND THE LAST LINE OF THE MACRO BELOW HAS NO TRAILING BACKSLASH. Appending
//  an alias means editing that line too; forget it and every alias after the
//  break silently vanishes.
// ============================================================================

#define ENGINE_SCRIPT_COMPONENT_REFERENCES                                     \
    using TransformComponent =                                                 \
        ::eng::ComponentRef<::eng::TransformComponent>;                        \
    using SpriteComponent = ::eng::ComponentRef<::eng::SpriteComponent>;       \
    using SpriteAnimationComponent =                                           \
        ::eng::ComponentRef<::eng::SpriteAnimationComponent>;                  \
    using Rigidbody2DComponent =                                               \
        ::eng::ComponentRef<::eng::Rigidbody2DComponent>;                      \
    using CameraComponent = ::eng::ComponentRef<::eng::CameraComponent>;       \
    using TilemapComponent = ::eng::ComponentRef<::eng::TilemapComponent>;     \
    using TilemapColliderComponent =                                           \
        ::eng::ComponentRef<::eng::TilemapColliderComponent>;                  \
    using SpinComponent = ::eng::ComponentRef<::eng::SpinComponent>;           \
    using BoxColliderComponent =                                               \
        ::eng::ComponentRef<::eng::BoxColliderComponent>;                      \
    using CircleColliderComponent =                                            \
        ::eng::ComponentRef<::eng::CircleColliderComponent>;                   \
    using CapsuleColliderComponent =                                           \
        ::eng::ComponentRef<::eng::CapsuleColliderComponent>;                  \
    using EdgeColliderComponent =                                              \
        ::eng::ComponentRef<::eng::EdgeColliderComponent>;                     \
    using PolygonColliderComponent =                                           \
        ::eng::ComponentRef<::eng::PolygonColliderComponent>;                  \
    using UIRectComponent = ::eng::ComponentRef<::eng::UIRectComponent>;       \
    using UIImageComponent = ::eng::ComponentRef<::eng::UIImageComponent>;     \
    using UITextComponent = ::eng::ComponentRef<::eng::UITextComponent>;       \
    using UIButtonComponent = ::eng::ComponentRef<::eng::UIButtonComponent>;   \
    using UISliderComponent = ::eng::ComponentRef<::eng::UISliderComponent>;   \
    using PlayerInputComponent =                                               \
        ::eng::ComponentRef<::eng::PlayerInputComponent>;
