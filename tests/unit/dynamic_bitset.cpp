#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "data_structures/bitset/dynamic_bitset.hpp"

namespace {

template <typename T>
class DynamicBitsetTest : public ::testing::Test {};
typedef ::testing::Types<DynamicBitset<>, DynamicBitset<uint8_t, 8>>
    DynamicBitsetTypes;
TYPED_TEST_CASE(DynamicBitsetTest, DynamicBitsetTypes);

TYPED_TEST(DynamicBitsetTest, BasicAtAndSet) {
  TypeParam db;

  EXPECT_EQ(db.at(17, 1), 0);
  EXPECT_EQ(db.at(17), false);
  db.set(17, 1);
  EXPECT_EQ(db.at(17, 1), 1);
  EXPECT_EQ(db.at(17), true);
}

TYPED_TEST(DynamicBitsetTest, GroupAt) {
  TypeParam db;

  db.set(0, 1);
  db.set(1, 1);
  EXPECT_EQ(db.at(0, 2), 1 | 2);
  db.set(3, 1);
  EXPECT_EQ(db.at(0, 2), 1 | 2);
  EXPECT_EQ(db.at(0, 3), 1 | 2);
  EXPECT_EQ(db.at(0, 4), 1 | 2 | 8);
  EXPECT_EQ(db.at(1, 1), 1);
  EXPECT_EQ(db.at(1, 2), 1);
  EXPECT_EQ(db.at(1, 3), 1 | 4);
}

TYPED_TEST(DynamicBitsetTest, GroupSet) {
  TypeParam db;
  EXPECT_EQ(db.at(0, 3), 0);
  db.set(1, 2);
  EXPECT_FALSE(db.at(0));
  EXPECT_TRUE(db.at(1));
  EXPECT_TRUE(db.at(2));
  EXPECT_FALSE(db.at(3));
}

class Clear : public ::testing::Test {
 protected:
  DynamicBitset<> db;

  void SetUp() override {
    db.set(17, 1);
    db.set(18, 1);
    EXPECT_EQ(db.at(17), true);
    EXPECT_EQ(db.at(18), true);
  }
};

TEST_F(Clear, OneElement) {
  db.clear(17, 1);
  EXPECT_EQ(db.at(17), false);
  EXPECT_EQ(db.at(18), true);
}

TEST_F(Clear, Group) {
  db.clear(17, 2);
  EXPECT_EQ(db.at(17), false);
  EXPECT_EQ(db.at(18), false);
}

TEST_F(Clear, EmptyGroup) {
  db.clear(17, 0);
  EXPECT_EQ(db.at(17), true);
  EXPECT_EQ(db.at(18), true);
}

TEST(DynamicBitset, ConstBitset) {
  auto const_accepting = [](const DynamicBitset<> &cdbs) {
    EXPECT_FALSE(cdbs.at(16));
    EXPECT_TRUE(cdbs.at(17));
    EXPECT_FALSE(cdbs.at(18));
  };

  DynamicBitset<> dbs;
  dbs.set(17);
  const_accepting(dbs);
}
}
