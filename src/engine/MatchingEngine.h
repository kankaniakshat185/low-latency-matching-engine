#pragma once

#include "engine/OrderBook.h"
#include "engine/Order.h"
#include "engine/Trade.h"
#include "engine/Types.h"
#include <cassert>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
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
//
// Templated on the book implementation (BookT) so Phase 4's comparative
// study can instantiate the exact same engine logic over multiple OrderBook
// implementations (OrderBook, OrderBookV2, ...) with zero virtual dispatch —
// this is the payoff of the composition-based design from ADR-0001/0002:
// swapping OrderBook's internals really does require no change here.
// BookT only needs to provide the same interface OrderBook does: hasOrder,
// addOrder, cancelOrder, matchAgainst.
template <typename BookT>
class MatchingEngineT {
public:
    // Forwards to BookT's constructor instead of a plain `= default`, so a
    // variant that needs construction arguments (e.g. OrderBookV2's
    // fixed-capacity pool size, ADR-0017) can be sized explicitly:
    // `MatchingEngineT<OrderBookV2> engine(capacity);`. A zero-argument call
    // still works exactly as before for BookT types with a default
    // constructor (OrderBook among them) — `book_()` value-initializes.
    // Not SFINAE-guarded against being selected for copy/move construction;
    // nothing in this codebase copies a MatchingEngineT today, and if that
    // ever changes, BookT lacking a matching constructor turns it into a
    // compile error rather than a silent bug.
    template <typename... Args>
    explicit MatchingEngineT(Args&&... args) : book_(std::forward<Args>(args)...) {}

    // Process a new incoming order. Returns a list of generated trades.
    // Throws std::invalid_argument if `order.quantity == 0` or if
    // `order.id` already belongs to a resting order in the book (duplicate
    // ids are rejected rather than silently corrupting the cancel index).
    std::vector<Trade> processOrder(Order order) {
        if (order.quantity == 0) {
            throw std::invalid_argument("MatchingEngine::processOrder: order quantity must be greater than zero (id=" +
                                        std::to_string(order.id) + ")");
        }
        if (book_.hasOrder(order.id)) {
            throw std::invalid_argument("MatchingEngine::processOrder: duplicate OrderId " + std::to_string(order.id) +
                                        " (already resting in the book)");
        }

        std::vector<Trade> trades;

        // Limit orders may only match at their limit price or better; market
        // orders sweep unconditionally, so pass no limit at all.
        std::optional<Price> limitPrice;
        if (order.type == OrderType::Limit) {
            limitPrice = order.price;
        }

        book_.matchAgainst(order, limitPrice, trades);

        // Only limit orders rest their unfilled remainder in the book; a
        // market order's unfilled remainder is discarded by design.
        if (order.type == OrderType::Limit && order.quantity > 0) {
            // Guaranteed to succeed: the duplicate-id check above already
            // confirmed this id isn't resting anywhere, and matching never
            // introduces a new id collision.
            [[maybe_unused]] bool added = book_.addOrder(order);
            assert(added && "OrderBook::addOrder rejected an id already validated as unique by processOrder");
        }

        return trades;
    }

    // Cancel an existing order in the book. Returns true if successful.
    [[nodiscard]] bool cancelOrder(OrderId id) { return book_.cancelOrder(id); }

    // Provide const access to the order book for inspection
    const BookT& getOrderBook() const { return book_; }

private:
    BookT book_;
};

// The concrete engine used everywhere today — Phase 1–3's baseline OrderBook
// (std::map/std::list/std::unordered_map). Every existing call site
// (`MatchingEngine engine;`) keeps working unchanged; this alias is the only
// thing that makes that true after the templating.
using MatchingEngine = MatchingEngineT<OrderBook>;

} // namespace engine
