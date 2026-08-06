#pragma once

#include "engine/Order.h"
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace engine {
namespace structures {

// Sentinel meaning "no node" — used for intrusive-list prev/next links and
// for "this price level's list is empty" (headIndex == kInvalidNodeIndex).
inline constexpr uint32_t kInvalidNodeIndex = std::numeric_limits<uint32_t>::max();

// One slot in the pool: an Order plus the intrusive doubly-linked-list
// pointers, stored as indices into the same pool rather than raw pointers.
// Indices stay meaningful across the pool's whole lifetime, are half the
// size of a pointer on a 64-bit build, and can't dangle in a way that's
// hard to spot in a debugger.
struct OrderNode {
    Order order{0, 0, 0, Side::Buy, OrderType::Limit}; // placeholder; overwritten by OrderPool::acquire
    uint32_t prev = kInvalidNodeIndex;
    uint32_t next = kInvalidNodeIndex;
};

// Fixed-capacity slab allocator for OrderNodes — see ADR-0017 for why fixed
// rather than growable. One heap allocation happens here, up front, at
// construction; after that, acquire()/release() are index bookkeeping only,
// no malloc/free. That's the entire point of 2.0: get allocation off the
// per-order hot path.
class OrderPool {
public:
    explicit OrderPool(size_t capacity) : nodes_(capacity), capacity_(capacity) {}

    // Acquires a free slot, fills it with `order`, returns its index.
    // Throws std::runtime_error if the pool is exhausted. Per ADR-0017,
    // growing or falling back to malloc here would reintroduce the exact
    // allocation-under-load cost this pool exists to eliminate — exhaustion
    // is a sizing bug to fix, not a case to handle gracefully.
    [[nodiscard]] uint32_t acquire(const Order& order) {
        uint32_t index;
        if (!freeList_.empty()) {
            index = freeList_.back();
            freeList_.pop_back();
        } else if (highWaterMark_ < capacity_) {
            index = static_cast<uint32_t>(highWaterMark_++);
        } else {
            throw std::runtime_error("OrderPool: capacity exhausted (" + std::to_string(capacity_) +
                                     " slots) — see ADR-0017: this pool does not grow or fall back to malloc.");
        }
        OrderNode& node = nodes_[index];
        node.order = order;
        node.prev = kInvalidNodeIndex;
        node.next = kInvalidNodeIndex;
        return index;
    }

    // Returns a previously-acquired slot to the free list for reuse.
    void release(uint32_t index) { freeList_.push_back(index); }

    OrderNode& operator[](uint32_t index) { return nodes_[index]; }
    const OrderNode& operator[](uint32_t index) const { return nodes_[index]; }

    size_t capacity() const { return capacity_; }

private:
    std::vector<OrderNode> nodes_; // sized once at construction, never reallocated after
    std::vector<uint32_t> freeList_;
    size_t highWaterMark_ = 0;
    size_t capacity_;
};

} // namespace structures
} // namespace engine
