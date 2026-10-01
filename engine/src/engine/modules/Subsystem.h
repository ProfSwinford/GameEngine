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
//  the order itself is at the top of Engine.cpp.
//
//  ==========================================================================
//  A SUBSYSTEM IS A CLASS THAT KNOWS HOW TO START AND STOP ITSELF. It says so
//  by inheriting from Subsystem below and filling in two functions:
//
//      class Log : public Subsystem {
//      public:
//          bool Init(const BootConfig& config) override;
//          void Shutdown() override;
//      };
//
//  The engine keeps one Log object and writes it down with a name:
//
//      m_subsystems.Add("Log", m_log);
//
//  NOTE WHAT THAT LINE DOES NOT DO: it does not start the log. It writes down
//  WHICH object to start, and where in the order. Nothing runs until InitAll
//  is called, further down in Engine::Init.
//
//  `override` is not decoration. It tells the compiler "this is meant to
//  replace the Init in Subsystem" and makes it check. Misspell the name or get
//  an argument wrong and you get an error, instead of a subsystem that quietly
//  never starts.
//
//  REGISTRATION ORDER IS DEPENDENCY ORDER. Each subsystem may assume
//  everything added before it is already running.
// ============================================================================

#include <string>
#include <vector>

namespace eng {

// The settings read from config/engine.json, NAMED here but not defined.
//
// Init below only takes a REFERENCE to one, and a reference needs the name of
// the type, not its contents. Including Config.h instead would be circular:
// Config.h includes Log.h, and Log.h includes this file.
struct BootConfig;

// Everything the engine starts and stops in order inherits from this.
class Subsystem {
public:
    // A base class that is inherited from needs a virtual destructor, so that
    // deleting through a Subsystem* runs the real class's destructor.
    virtual ~Subsystem() = default;

    // Open the file, make the window, connect to the sound card - whatever
    // this piece needs before anything can use it.
    //
    // Return false when it cannot start. Do NOT throw and do not exit: a
    // subsystem failing is usually the machine's fault (no display, a missing
    // file) and the engine has to be able to report it and stop tidily.
    //
    // The settings are handed in rather than fetched, so that a subsystem
    // never has to know where the settings came from - which also keeps the
    // log from having to know that an Engine exists.
    //
    // `= 0` means "every subsystem writes its own". A class that inherits from
    // Subsystem and forgets one of these will not compile, which is far better
    // than one that compiles and silently refuses to start.
    virtual bool Init(const BootConfig& config) = 0;

    // Close and release, in the opposite order to Init.
    virtual void Shutdown() = 0;
};

class SubsystemStack {
public:
    // Writes down one subsystem: a name, and the object to start. Nothing runs
    // here - the object's address is remembered, and its Init is called later
    // by InitAll. The object has to outlive the stack, which is why the engine
    // keeps its subsystems as members of itself.
    void Add(std::string name, Subsystem& subsystem);

    // Starts everything in the order it was added, writing each one to the log.
    //
    // If one of them fails, the ones that already started are shut down in
    // REVERSE order, the failing one is NOT shut down (it never started, and
    // shutting down something that never started is how a tidy-up crashes),
    // the ones after it are never touched, and this returns false so the
    // program can print a message and exit.
    //
    // The settings are passed in here, at the moment things actually start,
    // rather than being remembered by every entry when it was written down.
    bool InitAll(const BootConfig& config);

    // Shuts everything down in the exact reverse of the order it started.
    // Safe to call after a failed InitAll - that already unwound itself.
    void ShutdownAll();

    std::size_t Count() const { return m_entries.size(); }

private:
    // One row of the list: what it is called, and what to start.
    struct Entry {
        std::string name;
        Subsystem*  system = nullptr;
    };

    std::vector<Entry> m_entries;

    // How many of them are currently running, counted from the front. Shutdown
    // walks back down from here.
    std::size_t m_startedCount = 0;
};

} // namespace eng
