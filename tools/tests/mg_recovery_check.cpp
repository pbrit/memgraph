#include <optional>
#include <string>

#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include "database/single_node/graph_db.hpp"
#include "database/single_node/graph_db_accessor.hpp"
#include "durability/single_node/recovery.hpp"
#include "query/typed_value.hpp"

static const char *usage =
    "--durability-dir DURABILITY_DIR\n"
    "Check that Memgraph can recover the snapshot. This tool should be "
    "invoked through 'test_mg_import' wrapper, so as to check that 'mg_import' "
    "tools work correctly.\n";

DEFINE_string(durability_dir, "", "Path to where the durability directory");

class RecoveryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::string durability_dir(FLAGS_durability_dir);
    durability::RecoveryData recovery_data;
    durability::RecoverOnlySnapshot(durability_dir, &db_, &recovery_data,
                                    std::nullopt);
    durability::RecoveryTransactions recovery_transactions(&db_);
    durability::RecoverWal(durability_dir, &db_, &recovery_data,
                               &recovery_transactions);
    durability::RecoverIndexes(&db_, recovery_data.indexes);
  }

  database::GraphDb db_;
};

TEST_F(RecoveryTest, TestVerticesRecovered) {
  auto dba = db_.Access();
  EXPECT_EQ(dba.VerticesCount(), 10);
  EXPECT_EQ(dba.VerticesCount(dba.Label("Comment")), 5);
  for (const auto &vertex : dba.Vertices(dba.Label("Comment"), false)) {
    EXPECT_TRUE(vertex.has_label(dba.Label("Message")));
  }
  EXPECT_EQ(dba.VerticesCount(dba.Label("Forum")), 5);
}

TEST_F(RecoveryTest, TestPropertyNull) {
  auto dba = db_.Access();
  bool found = false;
  for (const auto &vertex : dba.Vertices(dba.Label("Comment"), false)) {
    auto id_prop = query::TypedValue(vertex.PropsAt(dba.Property("id")));
    auto browser = query::TypedValue(vertex.PropsAt(dba.Property("browser")));
    if (id_prop.IsString() && id_prop.ValueString() == "2") {
      EXPECT_FALSE(found);
      found = true;
      EXPECT_TRUE(browser.IsNull());
    } else {
      EXPECT_FALSE(browser.IsNull());
    }
  }
  ASSERT_TRUE(found);
}

TEST_F(RecoveryTest, TestEdgesRecovered) {
  auto dba = db_.Access();
  EXPECT_EQ(dba.EdgesCount(), 5);
  for (const auto &edge : dba.Edges(false)) {
    EXPECT_TRUE(edge.EdgeType() == dba.EdgeType("POSTED_ON"));
  }
}

TEST_F(RecoveryTest, TestQuote) {
  auto dba = db_.Access();
  for (const auto &vertex : dba.Vertices(dba.Label("Comment"), false)) {
    auto id_prop = query::TypedValue(vertex.PropsAt(dba.Property("id")));
    auto country = query::TypedValue(vertex.PropsAt(dba.Property("country")));
    if (id_prop.IsString() && id_prop.ValueString() == "1") {
      EXPECT_TRUE(country.IsString());
      EXPECT_EQ(country.ValueString(), "United Kingdom");
    }
  }
}

TEST_F(RecoveryTest, TestNodeLabelFlag) {
  auto dba = db_.Access();
  for (const auto &vertex : dba.Vertices(false)) {
    EXPECT_TRUE(vertex.has_label(dba.Label("First")));
    EXPECT_TRUE(vertex.has_label(dba.Label("Second")));
  }
}

TEST_F(RecoveryTest, TestRelationshipType) {
  auto dba = db_.Access();
  EXPECT_EQ(dba.EdgesCount(), 5);
  for (const auto &edge : dba.Edges(false)) {
    EXPECT_TRUE(edge.EdgeType() == dba.EdgeType("TYPE"));
  }
}

int main(int argc, char *argv[]) {
  ::testing::InitGoogleTest(&argc, argv);
  gflags::SetUsageMessage(usage);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  return RUN_ALL_TESTS();
}
