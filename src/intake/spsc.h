// Single-Producer Single-Consumer lock-free ring buffer.
//
// One producer thread calls try_push(); one consumer thread calls try_pop().
// Both are wait-free. Capacity is a compile-time power of two; head and tail
// are 64-bit free-running counters and the mask is applied at index time, so
// there is no ABA risk.
//
// Memory ordering: the producer publishes the slot value with release on the
// head store, and the consumer reads with acquire on the head load. Symmetric
// for the consumer's tail update. This is the classic Vyukov-style SPSC
// pattern; the safety argument is documented in docs/spsc-memory-ordering.md.
//
// The buffer stores move-constructed T. T must be nothrow-movable so that a
// stuck slot cannot leak the producer's index advance.
#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

namespace ledgercore::intake {

inline constexpr std::size_t kCacheLine = 64;

template <typename T, std::size_t Capacity>
class SpscRing {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static_assert(Capacity >= 2, "Capacity must be >= 2");
    static_assert(std::is_nothrow_move_constructible_v<T>, "T must be nothrow movable");

public:
    SpscRing() = default;
    SpscRing(const SpscRing&) = delete;
    SpscRing& operator=(const SpscRing&) = delete;

    // Producer side.
    bool try_push(T value) noexcept {
        const std::uint64_t head = head_.load(std::memory_order_relaxed);
        const std::uint64_t tail = tail_.load(std::memory_order_acquire);
        if (head - tail >= Capacity) return false;
        slot(head) = std::move(value);
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    // Consumer side.
    bool try_pop(T& out) noexcept {
        const std::uint64_t tail = tail_.load(std::memory_order_relaxed);
        const std::uint64_t head = head_.load(std::memory_order_acquire);
        if (tail == head) return false;
        out = std::move(slot(tail));
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    // Approximate occupancy. Snapshot is non-atomic across both counters but
    // adequate for diagnostics.
    std::size_t size_approx() const noexcept {
        return static_cast<std::size_t>(head_.load(std::memory_order_relaxed) -
                                        tail_.load(std::memory_order_relaxed));
    }

    static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    T& slot(std::uint64_t idx) noexcept {
        return *std::launder(reinterpret_cast<T*>(&storage_[idx & (Capacity - 1)]));
    }

    // Storage cell: explicit aligned byte buffer. std::aligned_storage_t is
    // deprecated as of C++23, so we keep the same memory layout by hand.
    struct alignas(T) Slot {
        unsigned char bytes[sizeof(T)];
    };

    // Cache-line separation between producer and consumer counters to avoid
    // false sharing on the busy hot path.
    alignas(kCacheLine) std::atomic<std::uint64_t> head_{0};
    alignas(kCacheLine) std::atomic<std::uint64_t> tail_{0};
    alignas(kCacheLine) std::array<Slot, Capacity> storage_{};
};

}  // namespace ledgercore::intake
