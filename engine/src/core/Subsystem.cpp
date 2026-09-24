// =============================================================================
//  Subsystem.cpp - a skeleton. Every function is here with the right signature
//  and an empty body. Subsystem.h is the specification; read it first.
// =============================================================================

#include <engine/core/Subsystem.h>
#include <engine/core/Log.h>

namespace eng {

// Adds a subsystem to the end of the list. The order they are added in IS the
// order they start in, and each one may assume everything before it is running.
void SubsystemStack::Add(std::string name, Subsystem& subsystem) {
    Entry entry;
    entry.name = std::move(name);
    entry.system = &subsystem;
    m_entries.push_back(std::move(entry));
}

// Starts every subsystem in order. If one fails, everything already started is
// shut down in reverse and this returns false - so a half-started engine never
// escapes this function.
bool SubsystemStack::InitAll(const BootConfig& config) {
    m_startedCount = 0;
    for (std::size_t i = 0; i < m_entries.size(); i++) {
        const Entry& entry = m_entries[i];

        if (entry.system->Init(config)) {
            ENGINE_LOG_INFO(Channels::kCore, "[{}/{}] {} started", i + 1, m_entries.size(),
                            entry.name);
            m_startedCount++;
            continue;
        }

        ENGINE_LOG_ERROR(Channels::kCore, "'{}' failed to start. Shutting down the {} that were started.", entry.name, m_startedCount);

        for (std::size_t j = i; j > 0; j--) {
            m_entries[j].system->Shutdown();
            ENGINE_LOG_INFO(Channels::kCore, "\t{} stopped", m_entries[j].name);
        }

        m_startedCount = 0;
        return false;
    }
    
    return true;
}

// Stops every started subsystem in the exact reverse of the order it was
// started in.
void SubsystemStack::ShutdownAll() {
    for (std::size_t i = m_startedCount; i > 0; i--) {
        m_entries[i].system->Shutdown();

        ENGINE_LOG_INFO(Channels::kCore, "[{}/{}] {} stopped", i + 1, m_entries.size(),
                        m_entries[i].name);
    }

    m_startedCount = 0;
}

} // namespace eng
