#include <atomic>
#include <cassert>
#include <chrono>
#include <memory>
#include <thread>

#include "common/rdp/FreeRdpProcessNative.h"
#include "common/rdp/RdpKeyboardHookState.h"

int main()
{
    // A captured press keeps its capture decision when the gate closes.
    {
        RdpKeyboardHookState state;
        assert(state.consumeKey(0x12, true, true));
        assert(state.consumeKey(0x12, false, false));
    }

    // A press that went to Windows keeps its release, even if capture opened
    // in between: Windows must see the key go up.
    {
        RdpKeyboardHookState state;
        assert(!state.consumeKey(0x11, true, false));
        assert(!state.consumeKey(0x11, false, true));
    }

    // Auto-repeat keeps the first press's decision.
    {
        RdpKeyboardHookState state;
        assert(!state.consumeKey(0x10, true, false));
        assert(!state.consumeKey(0x10, true, true));
        assert(!state.consumeKey(0x10, false, true));
    }

    // An already held key can first reach the hook as an auto-repeat.
    {
        RdpKeyboardHookState state;
        assert(!state.consumeKey(0x12, true, true, true));
        assert(!state.consumeKey(0x12, false, true));
    }

    // A key held before the hook existed has no press on record.
    {
        RdpKeyboardHookState state;
        assert(!state.consumeKey(0x5B, false, true));
    }

    // Releasing one modifier leaves the other side's capture alone.
    {
        RdpKeyboardHookState state;
        assert(state.consumeKey(0xA0, true, true));
        assert(state.consumeKey(0xA1, true, true));
        assert(state.consumeKey(0xA0, false, false));
        assert(state.consumeKey(0xA1, false, false));
    }

    // reset() drops the records, so the next release pairs with nothing.
    {
        RdpKeyboardHookState state;
        assert(state.consumeKey(0x12, true, true));
        state.reset();
        assert(!state.consumeKey(0x12, false, false));
    }

    // After focus/target transfer the old keys are consumed, never re-forwarded.
    {
        RdpKeyboardHookState state;
        assert(state.consumeKey(0x12, true, true));
        state.detachCapturedKeys();
        assert(state.hasCapturedKeys());
        assert(state.isDetached(0x12));
        assert(state.consumeKey(0x12, true, true));
        assert(state.isDetached(0x12));
        assert(state.consumeKey(0x12, false, true));
        assert(!state.hasCapturedKeys());
        assert(!state.isDetached(0x12));
        assert(state.consumeKey(0x12, true, true));
        assert(!state.isDetached(0x12));
    }


    // The loop keeps spinning while the lock is held by a callback and takes it
    // on the first observed free slot: a detach that returned early would hand
    // the owner route back while a callback was still inside.
    {
        NativeRdpContext context{};
        context.locked = std::make_shared<NativeRdpContext::InvokeLock>();
        context.locked->locked.store(true);

        std::atomic_bool returned{false};
        std::thread detach([&] { nativefreerdp_detach_owner(&context); returned.store(true); });
        for (int waited = 0; !returned.load() && waited < 1000; ++waited)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        assert(!returned.load());

        context.locked->locked.store(false);
        detach.join();
        assert(context.owner == nullptr);
    }
    return 0;
}
