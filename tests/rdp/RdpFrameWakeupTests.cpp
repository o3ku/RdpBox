#undef NDEBUG
#include <cassert>
#include <thread>

#include "common/rdp/RdpFrameWakeup.h"

int main()
{
    RdpFrameWakeup wakeup;
    assert(!wakeup.beginProcessing());
    assert(wakeup.request());
    for (int i = 0; i < 10000; ++i)
        assert(!wakeup.request());
    assert(wakeup.beginProcessing());
    // A frame arriving after rearming, even while consuming, queues a wake.
    std::thread producer([&]() { assert(wakeup.request()); });
    producer.join();
    assert(!wakeup.request());
    assert(wakeup.beginProcessing());
    assert(!wakeup.beginProcessing());
    assert(wakeup.request());
    wakeup.close();
    assert(!wakeup.beginProcessing());
    assert(!wakeup.request());
    // Closing a binding cannot be undone by a racing rearm/request.
    for (int i = 0; i < 100; ++i)
    {
        RdpFrameWakeup binding;
        assert(binding.request());
        std::thread consumer([&]() { binding.beginProcessing(); binding.request(); });
        binding.close();
        consumer.join();
        assert(!binding.request() && !binding.beginProcessing());
    }
}
