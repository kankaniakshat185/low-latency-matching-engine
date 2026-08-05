#include <gtest/gtest.h>
#include "replay/CSVParser.h"
#include <fstream>
#include <cstdio> // for std::remove

using namespace engine;
using namespace engine::replay;
using namespace engine::benchmark;

class CSVParserTest : public ::testing::Test {
protected:
    std::string testFilename = "test_data.csv";

    void SetUp() override {
        // Create a temporary CSV file
        std::ofstream file(testFilename);
        file << "Action,OrderId,Price,Quantity,Side\n"; // Header
        file << "I,1,100,10,B\n";
        file << "I,2,101,20,S\n";
        file << "C,1,0,0,B\n"; // Cancel
        file.close();
    }

    void TearDown() override {
        std::remove(testFilename.c_str());
    }
};

TEST_F(CSVParserTest, ParseValidFile) {
    auto actions = CSVParser::parseFile(testFilename);
    
    ASSERT_EQ(actions.size(), 3);

    // First action: Insert Buy
    EXPECT_EQ(actions[0].actionType, ActionType::Insert);
    EXPECT_EQ(actions[0].order.id, 1);
    EXPECT_EQ(actions[0].order.price, 100);
    EXPECT_EQ(actions[0].order.quantity, 10);
    EXPECT_EQ(actions[0].order.side, Side::Buy);

    // Second action: Insert Sell
    EXPECT_EQ(actions[1].actionType, ActionType::Insert);
    EXPECT_EQ(actions[1].order.id, 2);
    EXPECT_EQ(actions[1].order.price, 101);
    EXPECT_EQ(actions[1].order.quantity, 20);
    EXPECT_EQ(actions[1].order.side, Side::Sell);

    // Third action: Cancel
    EXPECT_EQ(actions[2].actionType, ActionType::Cancel);
    EXPECT_EQ(actions[2].order.id, 1);
}

TEST_F(CSVParserTest, FileDoesNotExist) {
    EXPECT_THROW({
        CSVParser::parseFile("non_existent_file.csv");
    }, std::runtime_error);
}

// ---------------------------------------------------------
// Regression tests for validation gaps found in the staff audit.
// Each writes one malformed row and asserts it is rejected loudly
// instead of silently becoming a different (but valid-looking) order.
// ---------------------------------------------------------

namespace {

// Writes a single CSV row (with header) to `filename` and returns it.
std::string writeSingleRowFile(const std::string& filename, const std::string& row) {
    std::ofstream file(filename);
    file << "Action,OrderId,Price,Quantity,Side\n";
    file << row << "\n";
    file.close();
    return filename;
}

} // namespace

TEST(CSVParserValidation, RejectsNegativePrice) {
    std::string filename = writeSingleRowFile("test_neg_price.csv", "I,1,-5,10,B");
    // Without the fix, std::stoull("-5") wraps to 18446744073709551611
    // instead of throwing — verified directly against libc++/libstdc++.
    EXPECT_THROW(CSVParser::parseFile(filename), std::runtime_error);
    std::remove(filename.c_str());
}

TEST(CSVParserValidation, RejectsNegativeQuantity) {
    std::string filename = writeSingleRowFile("test_neg_qty.csv", "I,1,100,-10,B");
    EXPECT_THROW(CSVParser::parseFile(filename), std::runtime_error);
    std::remove(filename.c_str());
}

TEST(CSVParserValidation, RejectsInvalidActionChar) {
    std::string filename = writeSingleRowFile("test_bad_action.csv", "X,1,100,10,B");
    EXPECT_THROW(CSVParser::parseFile(filename), std::runtime_error);
    std::remove(filename.c_str());
}

TEST(CSVParserValidation, RejectsInvalidSideChar) {
    std::string filename = writeSingleRowFile("test_bad_side.csv", "I,1,100,10,X");
    // Previously silently defaulted to Side::Buy instead of rejecting.
    EXPECT_THROW(CSVParser::parseFile(filename), std::runtime_error);
    std::remove(filename.c_str());
}

TEST(CSVParserValidation, RejectsShortRow) {
    // Missing the Side field entirely.
    std::string filename = writeSingleRowFile("test_short_row.csv", "I,1,100,10");
    EXPECT_THROW(CSVParser::parseFile(filename), std::runtime_error);
    std::remove(filename.c_str());
}

TEST(CSVParserValidation, RejectsTrailingGarbageInNumericField) {
    std::string filename = writeSingleRowFile("test_trailing_garbage.csv", "I,1,100abc,10,B");
    EXPECT_THROW(CSVParser::parseFile(filename), std::runtime_error);
    std::remove(filename.c_str());
}

TEST(CSVParserValidation, RejectsQuantityOutOfUint32Range) {
    // 2^32 = 4294967296, one past Quantity's (uint32_t) max.
    std::string filename = writeSingleRowFile("test_qty_overflow.csv", "I,1,100,4294967296,B");
    EXPECT_THROW(CSVParser::parseFile(filename), std::runtime_error);
    std::remove(filename.c_str());
}

TEST(CSVParserValidation, AcceptsWellFormedCancelRowWithZeroPlaceholders) {
    // Cancel rows legitimately carry 0,0 placeholders for Price/Quantity
    // (they're unused for cancels) — these are not negative and must
    // still parse successfully.
    std::string filename = writeSingleRowFile("test_valid_cancel.csv", "C,1,0,0,B");
    auto actions = CSVParser::parseFile(filename);
    ASSERT_EQ(actions.size(), 1);
    EXPECT_EQ(actions[0].actionType, ActionType::Cancel);
    std::remove(filename.c_str());
}
