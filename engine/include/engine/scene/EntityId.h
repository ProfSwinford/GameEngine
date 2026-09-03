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
