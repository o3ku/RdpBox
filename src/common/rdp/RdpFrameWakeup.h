#pragma once

#include <atomic>

// One queued wake per binding. Rearm before consuming so a concurrent frame
// either belongs to that consumption or posts the next wake, never gets lost.
class RdpFrameWakeup
{
public:
    bool request()
    {
        State expected = State::Ready;
        return m_state.compare_exchange_strong(expected, State::Pending);
    }

    bool beginProcessing()
    {
        State expected = State::Pending;
        return m_state.compare_exchange_strong(expected, State::Ready);
    }

    void close()
    {
        m_state.store(State::Closed);
    }

    bool isOpen() const
    {
        return m_state.load() != State::Closed;
    }

private:
    enum class State { Ready, Pending, Closed };
    std::atomic<State> m_state{State::Ready};
};
