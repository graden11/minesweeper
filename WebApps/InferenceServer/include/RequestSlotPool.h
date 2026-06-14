#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// fwd
namespace http { struct PerfTrace; }

/// Pre-allocated buffer slot for the entire request lifecycle.
///
/// Owned by RequestSlotPool.  Handlers acquire a slot, fill imageBytes,
/// pass the slot (via shared_ptr) to the batcher, and the batcher writes
/// the inference result into resultJson.  The async response lambda holds
/// a shared_ptr reference so the slot stays alive until the HTTP response
/// is sent, then it is automatically returned to the pool.
struct RequestSlot
{
    int id;
    std::atomic<bool> inUse{false};

    std::vector<uint8_t> imageBytes;   // raw JPEG/PNG bytes
    std::vector<float>    inputTensor; // preprocessed CHW float tensor
    std::string           resultJson;  // inference output JSON

    std::shared_ptr<http::PerfTrace> perfTrace;

    void reset()
    {
        imageBytes.clear();
        inputTensor.clear();
        resultJson.clear();
        perfTrace.reset();
        // inUse is NOT reset here — the pool does that
    }
};

/// Lock-free acquisition, mutex-protected scan.
///
/// Usage:
///   auto slot = pool.acquire();
///   if (!slot) { /* pool exhausted, fallback or 503 */ }
///   slot->imageBytes = ...;
///   // pass slot to batcher, capture in async lambda ...
///   // when shared_ptr refcount → 0, slot returns to pool automatically
class RequestSlotPool
{
public:
    /// @param numSlots           pool capacity (default 64)
    /// @param imageBytesReserve  pre-allocate this many bytes per slot (default 256 KB)
    /// @param tensorReserve      pre-allocate for input tensor (0 = lazy, per model)
    explicit RequestSlotPool(int numSlots = 64,
                             size_t imageBytesReserve = 256 * 1024,
                             size_t tensorReserve = 0);

    /// Acquire a free slot.  Returns nullptr when pool is exhausted.
    /// The returned shared_ptr has a custom deleter that returns the slot.
    std::shared_ptr<RequestSlot> acquire();

    int  capacity()  const { return static_cast<int>(slots_.size()); }
    int  available() const;

private:
    void release(RequestSlot* slot);

    std::vector<std::unique_ptr<RequestSlot>> slots_;
    mutable std::mutex mutex_;
};
