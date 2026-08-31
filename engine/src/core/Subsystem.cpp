// ============================================================================
//  Subsystem.cpp - starting and stopping the engine's pieces in order.
//  See Subsystem.h. The order itself is in Engine.cpp.
// ============================================================================

#include <engine/core/Log.h>
#include <engine/core/Subsystem.h>

namespace eng {

void SubsystemStack::Register(std::unique_ptr<Subsystem> subsystem) {
    if (subsystem != nullptr) {
        m_subsystems.push_back(std::move(subsystem));
    }
}

bool SubsystemStack::InitAll() {
    m_initialisedCount = 0;

    for (std::size_t i = 0; i < m_subsystems.size(); ++i) {
        Subsystem& subsystem = *m_subsystems[i];

        if (subsystem.Init()) {
            ENGINE_LOG_INFO(Channels::kCore, "  [{}/{}] {} started", i + 1,
                            m_subsystems.size(), subsystem.Name());
            ++m_initialisedCount;
            continue;
        }

        ENGINE_LOG_ERROR(Channels::kCore, "'{}' failed to start", subsystem.Name());

        // Unwind exactly what came up, in exactly the reverse order.
        //
        // The one that FAILED is not shut down - it never started, and shutting
        // down something that never started is how a tidy-up turns into a
        // second crash. The ones after it were never touched at all.
        //
        // `for (std::size_t j = i; j-- > 0;)` is the standard way to count
        // down through unsigned indices: it tests j, then decrements it, so
        // the loop covers i-1 down to 0 and stops without ever going negative.
        ENGINE_LOG_INFO(Channels::kCore, "shutting down the {} that did start",
                        m_initialisedCount);
        for (std::size_t j = i; j-- > 0;) {
            ENGINE_LOG_INFO(Channels::kCore, "  {} stopped", m_subsystems[j]->Name());
            m_subsystems[j]->Shutdown();
        }
        m_initialisedCount = 0;
        return false;
    }

    return true;
}

void SubsystemStack::ShutdownAll() {
    // The exact reverse of the order they started, and only as far as starting
    // actually got.
    for (std::size_t i = m_initialisedCount; i-- > 0;) {
        ENGINE_LOG_INFO(Channels::kCore, "  [{}/{}] {} stopped", i + 1,
                        m_subsystems.size(), m_subsystems[i]->Name());
        m_subsystems[i]->Shutdown();
    }
    m_initialisedCount = 0;
}

void SubsystemStack::ForEach(
    const std::function<void(const Subsystem&, bool)>& fn) const {
    for (std::size_t i = 0; i < m_subsystems.size(); ++i) {
        fn(*m_subsystems[i], i < m_initialisedCount);
    }
}

} // namespace eng
