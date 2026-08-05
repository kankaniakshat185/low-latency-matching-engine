#pragma once

#include "engine/OrderBook.h"
#include "engine/Trade.h"
#include <vector>

namespace engine {

// --- Error-handling policy (applies uniformly across this module) ---
//
// - std::invalid_argument: the *caller* passed something that violates a
//   precondition (zero quantity, a duplicate/already-resting OrderId).
//   These are programming errors at the call site, not routine outcomes —
//   they throw so they can never be silently ignored.
// - bool return value: an expected, routine outcome that is not an error —
//   e.g. cancelOrder(id) returning false for "no such order" is a normal
//   result a caller is expected to check, not exceptional control flow.
//   Every bool-returning method here is [[nodiscard]] so that check can't
//   be silently skipped.
// - std::runtime_error (see CSVParser): reserved for the trust boundary
//   where *external* data (files, network, replay input) is malformed —
//   distinct from invalid_argument because the failure originates outside
//   the program, not from a caller's precondition violation.
class MatchingEngine {
public:
    MatchingEngine() = default;

    // Process a new incoming order. Returns a list of generated trades.
    // Throws std::invalid_argument if `order.quantity == 0` or if
    // `order.id` already belongs to a resting order in the book (duplicate
    // ids are rejected rather than silently corrupting the cancel index).
    std::vector<Trade> processOrder(Order order);

    // Cancel an existing order in the book. Returns true if successful.
    [[nodiscard]] bool cancelOrder(OrderId id);

    // Provide const access to the order book for inspection
    const OrderBook& getOrderBook() const { return book_; }

private:
    OrderBook book_;
};

} // namespace engine
