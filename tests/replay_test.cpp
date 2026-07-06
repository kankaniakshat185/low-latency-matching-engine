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
