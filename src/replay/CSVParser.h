#pragma once

#include "benchmark/WorkloadGenerator.h"
#include "engine/Types.h"
#include "engine/Order.h"
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <iostream>

namespace engine {
namespace replay {

class CSVParser {
public:
    // Expected CSV Schema: Action,OrderId,Price,Quantity,Side
    // Action: 'I' for Insert, 'C' for Cancel
    // Side: 'B' for Buy, 'S' for Sell (only required for Insert)
    // Example: I,1,100,10,B
    
    static std::vector<benchmark::BenchmarkAction> parseFile(const std::string& filepath) {
        std::vector<benchmark::BenchmarkAction> actions;
        std::ifstream file(filepath);

        if (!file.is_open()) {
            throw std::runtime_error("Could not open file: " + filepath);
        }

        std::string line;
        
        // Skip header if present (optional check)
        // If the first line doesn't start with I or C, assume it's a header
        if (std::getline(file, line)) {
            if (line.empty() || (line[0] != 'I' && line[0] != 'C')) {
                // It's a header, skip it.
            } else {
                // It's a data row, parse it.
                actions.push_back(parseLine(line));
            }
        }

        while (std::getline(file, line)) {
            if (line.empty()) continue;
            actions.push_back(parseLine(line));
        }

        return actions;
    }

private:
    static benchmark::BenchmarkAction parseLine(const std::string& line) {
        std::stringstream ss(line);
        std::string token;

        // Action
        std::getline(ss, token, ',');
        if (token.empty()) throw std::runtime_error("Missing Action field");
        char actionChar = token[0];

        // OrderId
        std::getline(ss, token, ',');
        OrderId orderId = std::stoull(token);

        // Price
        std::getline(ss, token, ',');
        Price price = std::stoull(token);

        // Quantity
        std::getline(ss, token, ',');
        Quantity quantity = std::stoull(token);

        // Side
        std::getline(ss, token, ',');
        Side side = Side::Buy;
        if (!token.empty() && token[0] == 'S') {
            side = Side::Sell;
        }

        benchmark::ActionType actionType = (actionChar == 'C') ? benchmark::ActionType::Cancel : benchmark::ActionType::Insert;

        return {actionType, Order(orderId, price, quantity, side, OrderType::Limit)};
    }
};

} // namespace replay
} // namespace engine
