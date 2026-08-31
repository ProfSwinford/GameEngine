#pragma once

// ============================================================================
//  Subsystem.h - starting the engine's pieces up in a written-down order, and
//  shutting them down in exactly the reverse.
//
//  WHY THIS EXISTS
//  The engine is made of parts that depend on one another. The renderer needs
//  a window. The texture loader needs a renderer. Everything needs the log.
//  Start them in the wrong order and the failure is not obvious - it usually
//  works, and then one day it does not, on somebody else's machine.
//
//  C++ makes it worse: global objects in different .cpp files are created in
//  an order the standard does not define. A global log in one file and a
//  global renderer in another have no fixed relationship. It works, it keeps
//  working, and then somebody reorders two filenames in the build script and
//  it stops.
//
//  The fix is not clever: do not use global objects for these things. START
//  THEM EXPLICITLY, IN AN ORDER THAT IS WRITTEN DOWN. That is this file, and
//  the order itself is in Engine.cpp.
//
//  REGISTRATION ORDER IS DEPENDENCY ORDER. Each subsystem may assume
//  everything registered before it is already running.
// ============================================================================

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace eng {

// One startable, stoppable piece of the engine.
class Subsystem {
public:
    virtual ~Subsystem() = default;

    // Returns false when it cannot start.
    //
    // It does NOT throw and it does not crash the program. A subsystem failing
    // to start is usually a problem with the machine - no display, a missing
    // file, no sound device - and the engine has to be able to respond to that
    // and exit tidily rather than simply dying.
    virtual bool Init() = 0;

    virtual void Shutdown() = 0;
    virtual const char* Name() const = 0;
};

// A Subsystem built from two functions, for the many cases where starting up
// means "call this" and shutting down means "call that". It saves writing a
// dozen nearly identical classes without hiding the ordering, which is the
// part that actually matters.
//
// std::function is the standard "any callable thing" type, so the two
// arguments can be plain functions or lambdas.
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

// The list of subsystems, in the order they start.
class SubsystemStack {
public:
    void Register(std::unique_ptr<Subsystem> subsystem);

    // Starts everything in registration order, writing each one to the log.
    //
    // If one of them fails, the ones that already started are shut down in
    // REVERSE order, the failing one is NOT shut down (it never started, and
    // shutting down something that never started is how a tidy-up crashes),
    // the ones after it are never touched, and this returns false so the
    // program can print a message and exit.
    bool InitAll();

    // Shuts everything down in the exact reverse of the order it started.
    // Safe to call after a failed InitAll - that already unwound itself.
    void ShutdownAll();

    std::size_t Count() const { return m_subsystems.size(); }

    // Lists what is registered and whether it is currently running.
    void ForEach(const std::function<void(const Subsystem&, bool running)>& fn) const;

private:
    std::vector<std::unique_ptr<Subsystem>> m_subsystems;
    std::size_t                             m_initialisedCount = 0;
};

} // namespace eng
