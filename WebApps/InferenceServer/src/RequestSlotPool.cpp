#include "../include/RequestSlotPool.h"

RequestSlotPool::RequestSlotPool(int numSlots,
                                 size_t imageBytesReserve,
                                 size_t tensorReserve)
{
    slots_.reserve(numSlots);
    for (int i = 0; i < numSlots; ++i)
    {
        auto s = std::make_unique<RequestSlot>();
        s->id = i;
        if (imageBytesReserve > 0)
            s->imageBytes.reserve(imageBytesReserve);
        if (tensorReserve > 0)
            s->inputTensor.reserve(tensorReserve);
        slots_.push_back(std::move(s));
    }
}

std::shared_ptr<RequestSlot> RequestSlotPool::acquire()
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& s : slots_)
    {
        bool expected = false;
        if (s->inUse.compare_exchange_strong(expected, true))
        {
            s->reset();
            // Custom deleter: marks slot free so the pool can re-use it.
            return std::shared_ptr<RequestSlot>(s.get(),
                [this](RequestSlot* ptr) { release(ptr); });
        }
    }
    return nullptr;  // pool exhausted
}

int RequestSlotPool::available() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    int n = 0;
    for (auto& s : slots_)
        if (!s->inUse.load()) ++n;
    return n;
}

void RequestSlotPool::release(RequestSlot* slot)
{
    slot->inUse.store(false);
}
