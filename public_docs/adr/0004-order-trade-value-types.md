# ADR-0004: `Order`/`Trade` as plain value types, no polymorphism

Status: Accepted
Date: 2026-07-06

## Context

`Order` has to represent both limit and market orders and live by value inside `std::list<Order>` — a direct consequence of ADR-0002's iterator-stability requirement. Every field on it gets touched on the hot path, potentially millions of times a second.

## Decision

`Order` is one flat struct — `id`, `price`, `quantity`, `side`, `type` — no inheritance, no virtual methods, no variant. `Trade` is the same idea: `makerOrderId`, `takerOrderId`, `price`, `quantity`. Both trivially copyable.

## Alternatives considered

A `LimitOrder`/`MarketOrder` subclass split follows directly from ADR-0001 and got rejected for the same reason — heap allocation and pointer storage instead of value storage in the list, plus a virtual call on every access.

`std::variant<LimitOrder, MarketOrder>` was tempting for a moment but `std::visit` isn't free — it's a jump table on every access — to solve a problem (is `price` meaningful right now) that a plain enum-tagged field already solves for nothing. The `price` field on a market order is just unused, and that's documented on the struct rather than hidden.

`std::optional<Price>` instead of an always-present field was the third option, and it lost because it adds a branch and extra storage (the engaged flag) for a distinction the matching code already has for free via `order.type`.

## Consequences

Cheap to copy, cheap to store, zero indirection on access — what you want out of a hot-path value type. The one rough edge: a market order's `price` field exists but means nothing, which trips up a first read of the struct if the comment gets skipped. And `Trade` carries no timestamp at all right now, which is a real gap once persistence or an audit trail becomes relevant — deliberately deferred, not forgotten.
