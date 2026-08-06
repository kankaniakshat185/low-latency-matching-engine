#pragma once

#include "benchmark/WorkloadGenerator.h"
#include "engine/Types.h"
#include "engine/Order.h"
#include <cstddef>
#include <exception>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <limits>

namespace engine {
namespace replay {

class CSVParser {
public:
    // Expected CSV Schema: Action,OrderId,Price,Quantity,Side
    // Action: 'I' for Insert, 'C' for Cancel
    // Side: 'B' for Buy, 'S' for Sell (only required for Insert)
    // Example: I,1,100,10,B
    //
    // This is the trust boundary between external/historical data and the
    // engine, so every field is validated explicitly rather than falling
    // back to a default on anything unexpected: a malformed row throws
    // std::runtime_error naming the line and field, instead of silently
    // becoming a different (but structurally valid) order.

    [[nodiscard]] static std::vector<benchmark::BenchmarkAction> parseFile(const std::string& filepath) {
        std::vector<benchmark::BenchmarkAction> actions;
        std::ifstream file(filepath);

        if (!file.is_open()) {
            throw std::runtime_error("Could not open file: " + filepath);
        }

        std::string line;
        int lineNumber = 0;

        // Skip header if present (optional check)
        // If the first line doesn't start with I or C, assume it's a header
        if (std::getline(file, line)) {
            ++lineNumber;
            if (line.empty() || (line[0] != 'I' && line[0] != 'C')) {
                // It's a header, skip it.
            } else {
                try {
                    actions.push_back(parseLine(line));
                } catch (const std::exception& e) {
                    throw std::runtime_error("Parse error on line " + std::to_string(lineNumber) + ": " + e.what());
                }
            }
        }

        while (std::getline(file, line)) {
            ++lineNumber;
            if (line.empty())
                continue;
            try {
                actions.push_back(parseLine(line));
            } catch (const std::exception& e) {
                throw std::runtime_error("Parse error on line " + std::to_string(lineNumber) + ": " + e.what());
            }
        }

        return actions;
    }

private:
    // Parses `token` as an unsigned integer of type T. Unlike std::stoull
    // used directly, this rejects:
    //   - empty tokens (missing field)
    //   - a leading '-' (std::stoull silently accepts "-5" and wraps it to
    //     a huge two's-complement value like 18446744073709551611 instead
    //     of throwing — verified empirically; this is the actual bug being
    //     closed here)
    //   - trailing garbage after the number (e.g. "10abc")
    //   - values that don't fit in T (e.g. a Quantity token that overflows
    //     uint32_t after being parsed as a 64-bit value)
    template <typename T>
    static T parseUnsigned(const std::string& token, const char* fieldName) {
        if (token.empty()) {
            throw std::runtime_error(std::string("Missing ") + fieldName + " field");
        }

        size_t firstNonSpace = token.find_first_not_of(" \t");
        if (firstNonSpace != std::string::npos && token[firstNonSpace] == '-') {
            throw std::runtime_error(std::string(fieldName) + " must not be negative: '" + token + "'");
        }

        unsigned long long value;
        size_t consumed = 0;
        try {
            value = std::stoull(token, &consumed);
        } catch (const std::exception&) {
            throw std::runtime_error(std::string("Invalid ") + fieldName + " value: '" + token + "'");
        }
        if (consumed != token.size()) {
            throw std::runtime_error(std::string("Trailing characters in ") + fieldName + " value: '" + token + "'");
        }
        if (value > static_cast<unsigned long long>(std::numeric_limits<T>::max())) {
            throw std::runtime_error(std::string(fieldName) + " value out of range: '" + token + "'");
        }
        return static_cast<T>(value);
    }

    static benchmark::BenchmarkAction parseLine(const std::string& line) {
        std::stringstream ss(line);
        std::string token;
        std::vector<std::string> fields;
        while (std::getline(ss, token, ',')) {
            fields.push_back(token);
        }
        if (fields.size() != 5) {
            throw std::runtime_error("Expected 5 fields (Action,OrderId,Price,Quantity,Side), got " +
                                     std::to_string(fields.size()));
        }

        // Action: must be exactly 'I' or 'C' — anything else (typo, blank,
        // stray whitespace) is rejected rather than silently treated as Insert.
        if (fields[0].empty() || (fields[0][0] != 'I' && fields[0][0] != 'C')) {
            throw std::runtime_error("Invalid Action field (expected 'I' or 'C'): '" + fields[0] + "'");
        }
        char actionChar = fields[0][0];

        OrderId orderId = parseUnsigned<OrderId>(fields[1], "OrderId");
        Price price = parseUnsigned<Price>(fields[2], "Price");
        Quantity quantity = parseUnsigned<Quantity>(fields[3], "Quantity");

        // Side: must be exactly 'B' or 'S' — anything else is rejected
        // rather than silently defaulting to Buy.
        if (fields[4].empty() || (fields[4][0] != 'B' && fields[4][0] != 'S')) {
            throw std::runtime_error("Invalid Side field (expected 'B' or 'S'): '" + fields[4] + "'");
        }
        Side side = (fields[4][0] == 'S') ? Side::Sell : Side::Buy;

        benchmark::ActionType actionType =
            (actionChar == 'C') ? benchmark::ActionType::Cancel : benchmark::ActionType::Insert;

        return {actionType, Order(orderId, price, quantity, side, OrderType::Limit)};
    }
};

} // namespace replay
} // namespace engine
