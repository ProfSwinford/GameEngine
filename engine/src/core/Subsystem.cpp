// ============================================================================
//  Subsystem.cpp - starting and stopping the engine's pieces in order.
//  See Subsystem.h for why the order is written down rather than accidental.
// ============================================================================

#include <engine/core/Log.h>
#include <engine/core/Subsystem.h>

namespace eng {

void SubsystemStack::Add(std::string name, Subsystem& subsystem) {
    // Nothing is started here. The ADDRESS of the object is remembered, and
    // its Init only runs later, from InitAll.
    Entry entry;
    entry.name   = std::move(name);
    entry.system = &subsystem;
    m_entries.push_back(std::move(entry));
}

bool SubsystemStack::InitAll(const BootConfig& config) {
    m_startedCount = 0;

    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        const Entry& entry = m_entries[i];

        if (entry.system->Init(config)) {
            ENGINE_LOG_INFO(Channels::kCore, "  [{}/{}] {} started", i + 1,
                            m_entries.size(), entry.name);
            ++m_startedCount;
            continue;
        }

        ENGINE_LOG_ERROR(Channels::kCore, "'{}' failed to start", entry.name);

        // Unwind exactly what came up, in exactly the reverse order.
        //
        // The one that FAILED is not shut down - it never started, and shutting
        // down something that never started is how a tidy-up turns into a
        // second crash. The ones after it were never touched at all.
        //
        // `for (std::size_t j = i; j-- > 0;)` is the standard way to count down
        // through unsigned indices: it tests j, then decrements it, so the loop
        // covers i-1 down to 0 and stops without ever going negative.
        ENGINE_LOG_INFO(Channels::kCore, "shutting down the {} that did start",
                        m_startedCount);
        for (std::size_t j = i; j-- > 0;) {
            ENGINE_LOG_INFO(Channels::kCore, "  {} stopped", m_entries[j].name);
            m_entries[j].system->Shutdown();
        }
        m_startedCount = 0;
        return false;
    }

    return true;
}

void SubsystemStack::ShutdownAll() {
    // The exact reverse of the order they started, and only as far as starting
    // actually got. The "shutting down" heading is logged by Engine::Shutdown,
    // which is the only thing that knows whether this is a real shutdown or the
    // tidy-up after a start-up that never completed.
    for (std::size_t i = m_startedCount; i-- > 0;) {
        ENGINE_LOG_INFO(Channels::kCore, "  [{}/{}] {} stopped", i + 1,
                        m_entries.size(), m_entries[i].name);
        m_entries[i].system->Shutdown();
    }

    m_startedCount = 0;
}

} // namespace eng
