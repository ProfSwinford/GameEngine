#pragma once

// ============================================================================
//  EntityId.h - how one entity refers to another.
//
//  WHY NOT JUST USE AN Entity* POINTER
//  Entities are destroyed while the game is running - a pickup is collected,
//  an enemy dies. Anything still holding a raw pointer to it is then holding a
//  pointer to memory that has been reused for something else. Reading through
//  it does not crash reliably; it usually gives you nonsense, which is worse.
//
//  An EntityId is a pair of plain numbers instead:
//
//      index       which slot in the scene's list of entities
//      generation  which occupant of that slot
//
//  The scene bumps a slot's generation each time it is reused. So a saved id
//  saying "slot 7, generation 3" can be checked: if slot 7 is now on
//  generation 4, the entity being referred to is gone, and the scene says so
//  instead of handing back whatever now lives there.
//
//  This is why the editor's Inspector can hold on to a selection safely, and
//  why deleting the selected entity produces an empty Inspector rather than a
//  crash. Unity does the same thing with its instance IDs.
//
//  A default-constructed EntityId has index -1 and refers to nothing, so a
//  field somebody forgot to fill in is obviously empty rather than accidentally
//  pointing at the first entity in the scene.
// ============================================================================

#include <format>
#include <string>

namespace eng {

struct EntityId {
    int index      = -1;
    int generation = 0;

    bool IsNull() const { return index < 0; }

    // Two ids refer to the same entity only when BOTH parts match. A matching
    // index with a different generation means "the same slot, but the entity
    // that used to be in it has gone" - which is the whole reason the
    // generation is there.
    friend bool operator==(const EntityId& a, const EntityId& b) {
        return a.index == b.index && a.generation == b.generation;
    }
    friend bool operator!=(const EntityId& a, const EntityId& b) { return !(a == b); }

    // Written out rather than left to the compiler, because "what does it mean
    // for one id to be less than another?" has no meaning in the game - it is
    // needed only so an EntityId can be a key in a std::set or std::map, which
    // keep their contents in order. The collision system uses that to remember
    // which pairs were touching last step.
    //
    // Compare the index first; only if those are equal does the generation
    // decide. That is the same rule as alphabetical order on two-letter words.
    friend bool operator<(const EntityId& a, const EntityId& b) {
        if (a.index != b.index) {
            return a.index < b.index;
        }
        return a.generation < b.generation;
    }
};

} // namespace eng

// ---------------------------------------------------------------------------
//  So that "{}" prints an EntityId.
//
//      void OnCollisionEnter(eng::EntityId other) {
//          Debug::Log("hit by {}", other);          // hit by #12:3
//      }
//
//  An id is what every collision event and every physics query hands a
//  script, so it is the value most likely to be printed while working out why
//  something touched something else. It prints as `#index:generation`, and
//  BOTH NUMBERS ARE SHOWN ON PURPOSE: an id whose generation does not match
//  the slot's is the one failure this type exists to catch, and it is
//  invisible if only the index is printed.
//
//  A null id prints as `#null` rather than `#-1:0`, because -1 is an
//  implementation detail and "nothing" is the thing being said.
// ---------------------------------------------------------------------------
template <>
struct std::formatter<eng::EntityId> : std::formatter<std::string> {
    auto format(const eng::EntityId& id, std::format_context& context) const {
        if (id.IsNull()) {
            return std::formatter<std::string>::format(std::string{"#null"}, context);
        }
        return std::formatter<std::string>::format(
            "#" + std::to_string(id.index) + ":" + std::to_string(id.generation),
            context);
    }
};