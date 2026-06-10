#include <cassert>
#include <vector>

#include "rdp/RdpProcessEventQueueBehavior.h"

namespace
{
struct PostedEvent
{
    enum class Kind
    {
        State,
        Frame,
        Cursor,
        Certificate,
    };

    Kind kind = Kind::Frame;
    FreeRdpProcess::State state = FreeRdpProcess::State::Idle;
    std::uintptr_t generation = 0;
};

class FakeEventTarget final : public rdp::process_event_queue::EventTarget
{
public:
    bool canPost() const override
    {
        return alive;
    }

    void postStateChanged(FreeRdpProcess::State state, std::uintptr_t generation) override
    {
        events.push_back(PostedEvent{ PostedEvent::Kind::State, state, generation });
    }

    void postFrameUpdated(std::uintptr_t generation) override
    {
        events.push_back(PostedEvent{ PostedEvent::Kind::Frame, FreeRdpProcess::State::Idle, generation });
    }

    void postCursorUpdated(std::uintptr_t generation) override
    {
        events.push_back(PostedEvent{ PostedEvent::Kind::Cursor, FreeRdpProcess::State::Idle, generation });
    }

    void postCertificateRequest(std::uintptr_t generation) override
    {
        events.push_back(PostedEvent{ PostedEvent::Kind::Certificate, FreeRdpProcess::State::Idle, generation });
    }

    bool alive = true;
    std::vector<PostedEvent> events;
};
}

int main()
{
    {
        FakeEventTarget target;

        assert(rdp::process_event_queue::postStateChanged(target, FreeRdpProcess::State::Running, 7));
        assert(rdp::process_event_queue::postFrameUpdated(target, 7));
        assert(rdp::process_event_queue::postCursorUpdated(target, 7));
        assert(rdp::process_event_queue::postCertificateRequest(target, 7));

        assert(target.events.size() == 4);
        assert(target.events[0].kind == PostedEvent::Kind::State);
        assert(target.events[0].state == FreeRdpProcess::State::Running);
        assert(target.events[0].generation == 7);
        assert(target.events[1].kind == PostedEvent::Kind::Frame);
        assert(target.events[1].generation == 7);
        assert(target.events[2].kind == PostedEvent::Kind::Cursor);
        assert(target.events[2].generation == 7);
        assert(target.events[3].kind == PostedEvent::Kind::Certificate);
        assert(target.events[3].generation == 7);
    }

    {
        FakeEventTarget target;
        target.alive = false;

        assert(!rdp::process_event_queue::postStateChanged(target, FreeRdpProcess::State::Finished, 8));
        assert(!rdp::process_event_queue::postFrameUpdated(target, 8));
        assert(!rdp::process_event_queue::postCursorUpdated(target, 8));
        assert(!rdp::process_event_queue::postCertificateRequest(target, 8));

        assert(target.events.empty());
    }

    {
        std::optional<rdp::process_event_queue::PendingCertificateRequest> pending;
        FreeRdpProcess::CertificateChallenge challenge;
        challenge.host = L"rdp.example.test";
        challenge.port = 3390;
        challenge.commonName = L"rdp.example.test";
        challenge.changed = true;

        rdp::process_event_queue::storePendingCertificateRequest(pending, 9, challenge);
        assert(pending.has_value());
        assert(pending->generation == 9);
        assert(pending->challenge.host == L"rdp.example.test");
        assert(pending->challenge.port == 3390);
        assert(pending->challenge.commonName == L"rdp.example.test");
        assert(pending->challenge.changed);

        FreeRdpProcess::CertificateChallenge replacement;
        replacement.host = L"new.example.test";
        replacement.port = 3389;
        rdp::process_event_queue::storePendingCertificateRequest(pending, 10, replacement);
        assert(pending.has_value());
        assert(pending->generation == 10);
        assert(pending->challenge.host == L"new.example.test");
        assert(pending->challenge.port == 3389);
    }

    {
        FakeEventTarget target;
        target.alive = false;
        std::optional<rdp::process_event_queue::PendingCertificateRequest> pending;

        FreeRdpProcess::CertificateChallenge challenge;
        challenge.host = L"stored-without-window";
        rdp::process_event_queue::storePendingCertificateRequest(pending, 11, challenge);
        assert(!rdp::process_event_queue::postCertificateRequest(target, 11));

        assert(pending.has_value());
        assert(pending->generation == 11);
        assert(pending->challenge.host == L"stored-without-window");
        assert(target.events.empty());
    }

    return 0;
}
