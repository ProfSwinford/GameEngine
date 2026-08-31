#pragma once

// =============================================================================
//  WEEK 7 - explicit subsystem startup and shutdown. Ch. 6.1.
//
//  THE STATIC INITIALIZATION ORDER FIASCO, briefly, because it is why this
//  file exists: C++ guarantees the order of static initialisation WITHIN one
//  translation unit and guarantees NOTHING across translation units. A global
//  logger in Log.cpp and a global renderer in Renderer.cpp have no defined
//  construction order. It works. It keeps working. Then someone reorders two
//  files in a CMake list and it stops working, in release only, on one machine.
//
//  The fix is not clever: stop using static construction for subsystems and
//  START THEM EXPLICITLY, IN AN ORDER THAT IS WRITTEN DOWN.
//
//  The subsystems were not invented this week. Weeks 1-6 built them - logging,
//  the platform and window layer, event routing, the worker threads, debug
//  draw, the timer registry. This week made explicit an ordering that had been
//  implicit and fragile since Week 3.
//
//  Registration order IS dependency order: each subsystem may assume
//  everything registered before it is up. Simple and explicit beats clever; a
//  dependency graph with a topological sort is a fine Phase 2 project and a
//  bad use of Week 7.
// =============================================================================

#include <engine/core/Types.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace eng {

class Subsystem {
public:
    virtual ~Subsystem() = default;

    // Returns false on failure. Does NOT throw and does NOT assert - a
    // subsystem failing to start is an ENVIRONMENT problem (no audio device,
    // no display, a missing file), not a programmer error, and the engine must
    // be able to respond to it rather than die.
    virtual bool Init() = 0;

    virtual void Shutdown() = 0;

    virtual const char* Name() const = 0;
};

// A subsystem built from two callables, for the many cases where a subsystem
// is "call this static Init and that static Shutdown". Saves nine nearly
// identical classes without hiding the ordering, which is the thing that
// matters.
class LambdaSubsystem final : public Subsystem {
public:
    LambdaSubsystem(std::string name, std::function<bool()> init,
                    std::function<void()> shutdown)
        : m_name(std::move(name)), m_init(std::move(init)),
          m_shutdown(std::move(shutdown)) {}

    bool Init() override { return m_init ? m_init() : true; }
    void Shutdown() override { if (m_shutdown) { m_shutdown(); } }
    const char* Name() const override { return m_name.c_str(); }

private:
    std::string           m_name;
    std::function<bool()> m_init;
    std::function<void()> m_shutdown;
};

// The subsystem stack. Owned by Engine; separated from it so the ordering
// machinery can be read and tested without the frame loop attached.
class SubsystemStack {
public:
    void Register(std::unique_ptr<Subsystem> subsystem);

    // Initialises in registration order, logging each.
    //
    // *** THE PART THAT IS ACTUALLY GRADED: *** if subsystem N fails,
    // subsystems N-1..0 are shut down in REVERSE order, N is NOT shut down
    // (it never came up), subsystems after N are never initialised, nothing
    // leaks, and this returns false so the caller can exit non-zero with a
    // readable message.
    //
    // An ordered boot that has never been made to fail is an ordered boot that
    // has not been tested - see Engine::SetForcedFailure and the sandbox's
    // --fail-subsystem flag.
    bool InitAll();

    // Shuts down in EXACT REVERSE order. Safe to call after a failed InitAll
    // (it is a no-op: InitAll already unwound what it had started).
    void ShutdownAll();

    usize Count() const { return m_subsystems.size(); }
    void  ForEach(const std::function<void(const Subsystem&, bool up)>& fn) const;

    // If a subsystem with this name is registered, its Init() is forced to
    // return false. This is how the forced-failure verification is performed
    // without editing engine code to break it - see docs/week07-milestone2.md.
    void SetForcedFailure(std::string_view subsystemName);

private:
    std::vector<std::unique_ptr<Subsystem>> m_subsystems;
    usize                                   m_initialisedCount = 0;
    std::string                             m_forcedFailure;
};

} // namespace eng
