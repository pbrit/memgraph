#include <cmath>
#include <iterator>
#include <memory>
#include <unordered_map>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "database/single_node/graph_db_accessor.hpp"
#include "query/context.hpp"
#include "query/frontend/ast/ast.hpp"
#include "query/frontend/opencypher/parser.hpp"
#include "query/interpret/awesome_memgraph_functions.hpp"
#include "query/interpret/eval.hpp"
#include "query/interpret/frame.hpp"
#include "query/path.hpp"
#include "storage/common/types.hpp"
#include "utils/string.hpp"

#include "query_common.hpp"

using namespace query;
using query::test_common::ToList;
using testing::ElementsAre;
using testing::UnorderedElementsAre;

namespace {

class ExpressionEvaluatorTest : public ::testing::Test {
 protected:
  database::SingleNode db;
  std::unique_ptr<database::GraphDbAccessor> dba{db.Access()};

  AstStorage storage;
  EvaluationContext ctx;
  SymbolTable symbol_table;

  Frame frame{128};
  ExpressionEvaluator eval{&frame, symbol_table, ctx, dba.get(),
                           GraphView::OLD};

  Identifier *CreateIdentifierWithValue(std::string name,
                                        const TypedValue &value) {
    auto id = storage.Create<Identifier>(name, true);
    auto symbol = symbol_table.CreateSymbol(name, true);
    symbol_table[*id] = symbol;
    frame[symbol] = value;
    return id;
  }
};

TEST_F(ExpressionEvaluatorTest, OrOperator) {
  auto *op =
      storage.Create<OrOperator>(storage.Create<PrimitiveLiteral>(true),
                                 storage.Create<PrimitiveLiteral>(false));
  auto val1 = op->Accept(eval);
  ASSERT_EQ(val1.ValueBool(), true);
  op = storage.Create<OrOperator>(storage.Create<PrimitiveLiteral>(true),
                                  storage.Create<PrimitiveLiteral>(true));
  auto val2 = op->Accept(eval);
  ASSERT_EQ(val2.ValueBool(), true);
}

TEST_F(ExpressionEvaluatorTest, XorOperator) {
  auto *op =
      storage.Create<XorOperator>(storage.Create<PrimitiveLiteral>(true),
                                  storage.Create<PrimitiveLiteral>(false));
  auto val1 = op->Accept(eval);
  ASSERT_EQ(val1.ValueBool(), true);
  op = storage.Create<XorOperator>(storage.Create<PrimitiveLiteral>(true),
                                   storage.Create<PrimitiveLiteral>(true));
  auto val2 = op->Accept(eval);
  ASSERT_EQ(val2.ValueBool(), false);
}

TEST_F(ExpressionEvaluatorTest, AndOperator) {
  auto *op =
      storage.Create<AndOperator>(storage.Create<PrimitiveLiteral>(true),
                                  storage.Create<PrimitiveLiteral>(true));
  auto val1 = op->Accept(eval);
  ASSERT_EQ(val1.ValueBool(), true);
  op = storage.Create<AndOperator>(storage.Create<PrimitiveLiteral>(false),
                                   storage.Create<PrimitiveLiteral>(true));
  auto val2 = op->Accept(eval);
  ASSERT_EQ(val2.ValueBool(), false);
}

TEST_F(ExpressionEvaluatorTest, AndOperatorShortCircuit) {
  {
    auto *op =
        storage.Create<AndOperator>(storage.Create<PrimitiveLiteral>(false),
                                    storage.Create<PrimitiveLiteral>(5));
    auto value = op->Accept(eval);
    EXPECT_EQ(value.ValueBool(), false);
  }
  {
    auto *op =
        storage.Create<AndOperator>(storage.Create<PrimitiveLiteral>(5),
                                    storage.Create<PrimitiveLiteral>(false));
    // We are evaluating left to right, so we don't short circuit here and
    // raise due to `5`. This differs from neo4j, where they evaluate both
    // sides and return `false` without checking for type of the first
    // expression.
    EXPECT_THROW(op->Accept(eval), QueryRuntimeException);
  }
}

TEST_F(ExpressionEvaluatorTest, AndOperatorNull) {
  {
    // Null doesn't short circuit
    auto *op = storage.Create<AndOperator>(
        storage.Create<PrimitiveLiteral>(PropertyValue::Null),
        storage.Create<PrimitiveLiteral>(5));
    EXPECT_THROW(op->Accept(eval), QueryRuntimeException);
  }
  {
    auto *op = storage.Create<AndOperator>(
        storage.Create<PrimitiveLiteral>(PropertyValue::Null),
        storage.Create<PrimitiveLiteral>(true));
    auto value = op->Accept(eval);
    EXPECT_TRUE(value.IsNull());
  }
  {
    auto *op = storage.Create<AndOperator>(
        storage.Create<PrimitiveLiteral>(PropertyValue::Null),
        storage.Create<PrimitiveLiteral>(false));
    auto value = op->Accept(eval);
    ASSERT_TRUE(value.IsBool());
    EXPECT_EQ(value.ValueBool(), false);
  }
}

TEST_F(ExpressionEvaluatorTest, AdditionOperator) {
  auto *op = storage.Create<AdditionOperator>(
      storage.Create<PrimitiveLiteral>(2), storage.Create<PrimitiveLiteral>(3));
  auto value = op->Accept(eval);
  ASSERT_EQ(value.ValueInt(), 5);
}

TEST_F(ExpressionEvaluatorTest, SubtractionOperator) {
  auto *op = storage.Create<SubtractionOperator>(
      storage.Create<PrimitiveLiteral>(2), storage.Create<PrimitiveLiteral>(3));
  auto value = op->Accept(eval);
  ASSERT_EQ(value.ValueInt(), -1);
}

TEST_F(ExpressionEvaluatorTest, MultiplicationOperator) {
  auto *op = storage.Create<MultiplicationOperator>(
      storage.Create<PrimitiveLiteral>(2), storage.Create<PrimitiveLiteral>(3));
  auto value = op->Accept(eval);
  ASSERT_EQ(value.ValueInt(), 6);
}

TEST_F(ExpressionEvaluatorTest, DivisionOperator) {
  auto *op =
      storage.Create<DivisionOperator>(storage.Create<PrimitiveLiteral>(50),
                                       storage.Create<PrimitiveLiteral>(10));
  auto value = op->Accept(eval);
  ASSERT_EQ(value.ValueInt(), 5);
}

TEST_F(ExpressionEvaluatorTest, ModOperator) {
  auto *op = storage.Create<ModOperator>(storage.Create<PrimitiveLiteral>(65),
                                         storage.Create<PrimitiveLiteral>(10));
  auto value = op->Accept(eval);
  ASSERT_EQ(value.ValueInt(), 5);
}

TEST_F(ExpressionEvaluatorTest, EqualOperator) {
  auto *op =
      storage.Create<EqualOperator>(storage.Create<PrimitiveLiteral>(10),
                                    storage.Create<PrimitiveLiteral>(15));
  auto val1 = op->Accept(eval);
  ASSERT_EQ(val1.ValueBool(), false);
  op = storage.Create<EqualOperator>(storage.Create<PrimitiveLiteral>(15),
                                     storage.Create<PrimitiveLiteral>(15));
  auto val2 = op->Accept(eval);
  ASSERT_EQ(val2.ValueBool(), true);
  op = storage.Create<EqualOperator>(storage.Create<PrimitiveLiteral>(20),
                                     storage.Create<PrimitiveLiteral>(15));
  auto val3 = op->Accept(eval);
  ASSERT_EQ(val3.ValueBool(), false);
}

TEST_F(ExpressionEvaluatorTest, NotEqualOperator) {
  auto *op =
      storage.Create<NotEqualOperator>(storage.Create<PrimitiveLiteral>(10),
                                       storage.Create<PrimitiveLiteral>(15));
  auto val1 = op->Accept(eval);
  ASSERT_EQ(val1.ValueBool(), true);
  op = storage.Create<NotEqualOperator>(storage.Create<PrimitiveLiteral>(15),
                                        storage.Create<PrimitiveLiteral>(15));
  auto val2 = op->Accept(eval);
  ASSERT_EQ(val2.ValueBool(), false);
  op = storage.Create<NotEqualOperator>(storage.Create<PrimitiveLiteral>(20),
                                        storage.Create<PrimitiveLiteral>(15));
  auto val3 = op->Accept(eval);
  ASSERT_EQ(val3.ValueBool(), true);
}

TEST_F(ExpressionEvaluatorTest, LessOperator) {
  auto *op = storage.Create<LessOperator>(storage.Create<PrimitiveLiteral>(10),
                                          storage.Create<PrimitiveLiteral>(15));
  auto val1 = op->Accept(eval);
  ASSERT_EQ(val1.ValueBool(), true);
  op = storage.Create<LessOperator>(storage.Create<PrimitiveLiteral>(15),
                                    storage.Create<PrimitiveLiteral>(15));
  auto val2 = op->Accept(eval);
  ASSERT_EQ(val2.ValueBool(), false);
  op = storage.Create<LessOperator>(storage.Create<PrimitiveLiteral>(20),
                                    storage.Create<PrimitiveLiteral>(15));
  auto val3 = op->Accept(eval);
  ASSERT_EQ(val3.ValueBool(), false);
}

TEST_F(ExpressionEvaluatorTest, GreaterOperator) {
  auto *op =
      storage.Create<GreaterOperator>(storage.Create<PrimitiveLiteral>(10),
                                      storage.Create<PrimitiveLiteral>(15));
  auto val1 = op->Accept(eval);
  ASSERT_EQ(val1.ValueBool(), false);
  op = storage.Create<GreaterOperator>(storage.Create<PrimitiveLiteral>(15),
                                       storage.Create<PrimitiveLiteral>(15));
  auto val2 = op->Accept(eval);
  ASSERT_EQ(val2.ValueBool(), false);
  op = storage.Create<GreaterOperator>(storage.Create<PrimitiveLiteral>(20),
                                       storage.Create<PrimitiveLiteral>(15));
  auto val3 = op->Accept(eval);
  ASSERT_EQ(val3.ValueBool(), true);
}

TEST_F(ExpressionEvaluatorTest, LessEqualOperator) {
  auto *op =
      storage.Create<LessEqualOperator>(storage.Create<PrimitiveLiteral>(10),
                                        storage.Create<PrimitiveLiteral>(15));
  auto val1 = op->Accept(eval);
  ASSERT_EQ(val1.ValueBool(), true);
  op = storage.Create<LessEqualOperator>(storage.Create<PrimitiveLiteral>(15),
                                         storage.Create<PrimitiveLiteral>(15));
  auto val2 = op->Accept(eval);
  ASSERT_EQ(val2.ValueBool(), true);
  op = storage.Create<LessEqualOperator>(storage.Create<PrimitiveLiteral>(20),
                                         storage.Create<PrimitiveLiteral>(15));
  auto val3 = op->Accept(eval);
  ASSERT_EQ(val3.ValueBool(), false);
}

TEST_F(ExpressionEvaluatorTest, GreaterEqualOperator) {
  auto *op = storage.Create<GreaterEqualOperator>(
      storage.Create<PrimitiveLiteral>(10),
      storage.Create<PrimitiveLiteral>(15));
  auto val1 = op->Accept(eval);
  ASSERT_EQ(val1.ValueBool(), false);
  op = storage.Create<GreaterEqualOperator>(
      storage.Create<PrimitiveLiteral>(15),
      storage.Create<PrimitiveLiteral>(15));
  auto val2 = op->Accept(eval);
  ASSERT_EQ(val2.ValueBool(), true);
  op = storage.Create<GreaterEqualOperator>(
      storage.Create<PrimitiveLiteral>(20),
      storage.Create<PrimitiveLiteral>(15));
  auto val3 = op->Accept(eval);
  ASSERT_EQ(val3.ValueBool(), true);
}

TEST_F(ExpressionEvaluatorTest, InListOperator) {
  auto *list_literal = storage.Create<ListLiteral>(std::vector<Expression *>{
      storage.Create<PrimitiveLiteral>(1), storage.Create<PrimitiveLiteral>(2),
      storage.Create<PrimitiveLiteral>("a")});
  {
    // Element exists in list.
    auto *op = storage.Create<InListOperator>(
        storage.Create<PrimitiveLiteral>(2), list_literal);
    auto value = op->Accept(eval);
    EXPECT_EQ(value.ValueBool(), true);
  }
  {
    // Element doesn't exist in list.
    auto *op = storage.Create<InListOperator>(
        storage.Create<PrimitiveLiteral>("x"), list_literal);
    auto value = op->Accept(eval);
    EXPECT_EQ(value.ValueBool(), false);
  }
  {
    auto *list_literal = storage.Create<ListLiteral>(std::vector<Expression *>{
        storage.Create<PrimitiveLiteral>(PropertyValue::Null),
        storage.Create<PrimitiveLiteral>(2),
        storage.Create<PrimitiveLiteral>("a")});
    // Element doesn't exist in list with null element.
    auto *op = storage.Create<InListOperator>(
        storage.Create<PrimitiveLiteral>("x"), list_literal);
    auto value = op->Accept(eval);
    EXPECT_TRUE(value.IsNull());
  }
  {
    // Null list.
    auto *op = storage.Create<InListOperator>(
        storage.Create<PrimitiveLiteral>("x"),
        storage.Create<PrimitiveLiteral>(PropertyValue::Null));
    auto value = op->Accept(eval);
    EXPECT_TRUE(value.IsNull());
  }
  {
    // Null literal.
    auto *op = storage.Create<InListOperator>(
        storage.Create<PrimitiveLiteral>(PropertyValue::Null), list_literal);
    auto value = op->Accept(eval);
    EXPECT_TRUE(value.IsNull());
  }
  {
    // Null literal, empty list.
    auto *op = storage.Create<InListOperator>(
        storage.Create<PrimitiveLiteral>(PropertyValue::Null),
        storage.Create<ListLiteral>(std::vector<Expression *>()));
    auto value = op->Accept(eval);
    EXPECT_FALSE(value.ValueBool());
  }
}

TEST_F(ExpressionEvaluatorTest, ListIndexing) {
  auto *list_literal = storage.Create<ListLiteral>(std::vector<Expression *>{
      storage.Create<PrimitiveLiteral>(1), storage.Create<PrimitiveLiteral>(2),
      storage.Create<PrimitiveLiteral>(3),
      storage.Create<PrimitiveLiteral>(4)});
  {
    // Legal indexing.
    auto *op = storage.Create<SubscriptOperator>(
        list_literal, storage.Create<PrimitiveLiteral>(2));
    auto value = op->Accept(eval);
    EXPECT_EQ(value.ValueInt(), 3);
  }
  {
    // Out of bounds indexing.
    auto *op = storage.Create<SubscriptOperator>(
        list_literal, storage.Create<PrimitiveLiteral>(4));
    auto value = op->Accept(eval);
    EXPECT_TRUE(value.IsNull());
  }
  {
    // Out of bounds indexing with negative bound.
    auto *op = storage.Create<SubscriptOperator>(
        list_literal, storage.Create<PrimitiveLiteral>(-100));
    auto value = op->Accept(eval);
    EXPECT_TRUE(value.IsNull());
  }
  {
    // Legal indexing with negative index.
    auto *op = storage.Create<SubscriptOperator>(
        list_literal, storage.Create<PrimitiveLiteral>(-2));
    auto value = op->Accept(eval);
    EXPECT_EQ(value.ValueInt(), 3);
  }
  {
    // Indexing with one operator being null.
    auto *op = storage.Create<SubscriptOperator>(
        storage.Create<PrimitiveLiteral>(PropertyValue::Null),
        storage.Create<PrimitiveLiteral>(-2));
    auto value = op->Accept(eval);
    EXPECT_TRUE(value.IsNull());
  }
  {
    // Indexing with incompatible type.
    auto *op = storage.Create<SubscriptOperator>(
        list_literal, storage.Create<PrimitiveLiteral>("bla"));
    EXPECT_THROW(op->Accept(eval), QueryRuntimeException);
  }
}

TEST_F(ExpressionEvaluatorTest, MapIndexing) {
  auto *map_literal = storage.Create<MapLiteral>(
      std::unordered_map<std::pair<std::string, storage::Property>,
                         Expression *>{{std::make_pair("a", dba->Property("a")),
                                        storage.Create<PrimitiveLiteral>(1)},
                                       {std::make_pair("b", dba->Property("b")),
                                        storage.Create<PrimitiveLiteral>(2)},
                                       {std::make_pair("c", dba->Property("c")),
                                        storage.Create<PrimitiveLiteral>(3)}});
  {
    // Legal indexing.
    auto *op = storage.Create<SubscriptOperator>(
        map_literal, storage.Create<PrimitiveLiteral>("b"));
    auto value = op->Accept(eval);
    EXPECT_EQ(value.ValueInt(), 2);
  }
  {
    // Legal indexing, non-existing key.
    auto *op = storage.Create<SubscriptOperator>(
        map_literal, storage.Create<PrimitiveLiteral>("z"));
    auto value = op->Accept(eval);
    EXPECT_TRUE(value.IsNull());
  }
  {
    // Wrong key type.
    auto *op = storage.Create<SubscriptOperator>(
        map_literal, storage.Create<PrimitiveLiteral>(42));
    EXPECT_THROW(op->Accept(eval), QueryRuntimeException);
  }
  {
    // Indexing with Null.
    auto *op = storage.Create<SubscriptOperator>(
        map_literal, storage.Create<PrimitiveLiteral>(PropertyValue::Null));
    auto value = op->Accept(eval);
    EXPECT_TRUE(value.IsNull());
  }
}

TEST_F(ExpressionEvaluatorTest, VertexAndEdgeIndexing) {
  auto edge_type = dba->EdgeType("edge_type");
  auto prop = dba->Property("prop");
  auto v1 = dba->InsertVertex();
  auto e11 = dba->InsertEdge(v1, v1, edge_type);
  v1.PropsSet(prop, 42);
  e11.PropsSet(prop, 43);

  auto *vertex_id = CreateIdentifierWithValue("v1", v1);
  auto *edge_id = CreateIdentifierWithValue("e11", e11);
  {
    // Legal indexing.
    auto *op1 = storage.Create<SubscriptOperator>(
        vertex_id, storage.Create<PrimitiveLiteral>("prop"));
    auto value1 = op1->Accept(eval);
    EXPECT_EQ(value1.ValueInt(), 42);

    auto *op2 = storage.Create<SubscriptOperator>(
        edge_id, storage.Create<PrimitiveLiteral>("prop"));
    auto value2 = op2->Accept(eval);
    EXPECT_EQ(value2.ValueInt(), 43);
  }
  {
    // Legal indexing, non-existing key.
    auto *op1 = storage.Create<SubscriptOperator>(
        vertex_id, storage.Create<PrimitiveLiteral>("blah"));
    auto value1 = op1->Accept(eval);
    EXPECT_TRUE(value1.IsNull());

    auto *op2 = storage.Create<SubscriptOperator>(
        edge_id, storage.Create<PrimitiveLiteral>("blah"));
    auto value2 = op2->Accept(eval);
    EXPECT_TRUE(value2.IsNull());
  }
  {
    // Wrong key type.
    auto *op1 = storage.Create<SubscriptOperator>(
        vertex_id, storage.Create<PrimitiveLiteral>(1));
    EXPECT_THROW(op1->Accept(eval), QueryRuntimeException);

    auto *op2 = storage.Create<SubscriptOperator>(
        edge_id, storage.Create<PrimitiveLiteral>(1));
    EXPECT_THROW(op2->Accept(eval), QueryRuntimeException);
  }
  {
    // Indexing with Null.
    auto *op1 = storage.Create<SubscriptOperator>(
        vertex_id, storage.Create<PrimitiveLiteral>(PropertyValue::Null));
    auto value1 = op1->Accept(eval);
    EXPECT_TRUE(value1.IsNull());

    auto *op2 = storage.Create<SubscriptOperator>(
        edge_id, storage.Create<PrimitiveLiteral>(PropertyValue::Null));
    auto value2 = op2->Accept(eval);
    EXPECT_TRUE(value2.IsNull());
  }
}

TEST_F(ExpressionEvaluatorTest, ListSlicingOperator) {
  auto *list_literal = storage.Create<ListLiteral>(std::vector<Expression *>{
      storage.Create<PrimitiveLiteral>(1), storage.Create<PrimitiveLiteral>(2),
      storage.Create<PrimitiveLiteral>(3),
      storage.Create<PrimitiveLiteral>(4)});

  auto extract_ints = [](TypedValue list) {
    std::vector<int64_t> int_list;
    for (auto x : list.ValueList()) {
      int_list.push_back(x.ValueInt());
    }
    return int_list;
  };
  {
    // Legal slicing with both bounds defined.
    auto *op = storage.Create<ListSlicingOperator>(
        list_literal, storage.Create<PrimitiveLiteral>(2),
        storage.Create<PrimitiveLiteral>(4));
    auto value = op->Accept(eval);
    EXPECT_THAT(extract_ints(value), ElementsAre(3, 4));
  }
  {
    // Legal slicing with negative bound.
    auto *op = storage.Create<ListSlicingOperator>(
        list_literal, storage.Create<PrimitiveLiteral>(2),
        storage.Create<PrimitiveLiteral>(-1));
    auto value = op->Accept(eval);
    EXPECT_THAT(extract_ints(value), ElementsAre(3));
  }
  {
    // Lower bound larger than upper bound.
    auto *op = storage.Create<ListSlicingOperator>(
        list_literal, storage.Create<PrimitiveLiteral>(2),
        storage.Create<PrimitiveLiteral>(-4));
    auto value = op->Accept(eval);
    EXPECT_THAT(extract_ints(value), ElementsAre());
  }
  {
    // Bounds ouf or range.
    auto *op = storage.Create<ListSlicingOperator>(
        list_literal, storage.Create<PrimitiveLiteral>(-100),
        storage.Create<PrimitiveLiteral>(10));
    auto value = op->Accept(eval);
    EXPECT_THAT(extract_ints(value), ElementsAre(1, 2, 3, 4));
  }
  {
    // Lower bound undefined.
    auto *op = storage.Create<ListSlicingOperator>(
        list_literal, nullptr, storage.Create<PrimitiveLiteral>(3));
    auto value = op->Accept(eval);
    EXPECT_THAT(extract_ints(value), ElementsAre(1, 2, 3));
  }
  {
    // Upper bound undefined.
    auto *op = storage.Create<ListSlicingOperator>(
        list_literal, storage.Create<PrimitiveLiteral>(-2), nullptr);
    auto value = op->Accept(eval);
    EXPECT_THAT(extract_ints(value), ElementsAre(3, 4));
  }
  {
    // Bound of illegal type and null value bound.
    auto *op = storage.Create<ListSlicingOperator>(
        list_literal, storage.Create<PrimitiveLiteral>(PropertyValue::Null),
        storage.Create<PrimitiveLiteral>("mirko"));
    EXPECT_THROW(op->Accept(eval), QueryRuntimeException);
  }
  {
    // List of illegal type.
    auto *op = storage.Create<ListSlicingOperator>(
        storage.Create<PrimitiveLiteral>("a"),
        storage.Create<PrimitiveLiteral>(-2), nullptr);
    EXPECT_THROW(op->Accept(eval), QueryRuntimeException);
  }
  {
    // Null value list with undefined upper bound.
    auto *op = storage.Create<ListSlicingOperator>(
        storage.Create<PrimitiveLiteral>(PropertyValue::Null),
        storage.Create<PrimitiveLiteral>(-2), nullptr);
    auto value = op->Accept(eval);
    EXPECT_TRUE(value.IsNull());
    ;
  }
  {
    // Null value index.
    auto *op = storage.Create<ListSlicingOperator>(
        list_literal, storage.Create<PrimitiveLiteral>(-2),
        storage.Create<PrimitiveLiteral>(PropertyValue::Null));
    auto value = op->Accept(eval);
    EXPECT_TRUE(value.IsNull());
    ;
  }
}

TEST_F(ExpressionEvaluatorTest, IfOperator) {
  auto *then_expression = storage.Create<PrimitiveLiteral>(10);
  auto *else_expression = storage.Create<PrimitiveLiteral>(20);
  {
    auto *condition_true =
        storage.Create<EqualOperator>(storage.Create<PrimitiveLiteral>(2),
                                      storage.Create<PrimitiveLiteral>(2));
    auto *op = storage.Create<IfOperator>(condition_true, then_expression,
                                          else_expression);
    auto value = op->Accept(eval);
    ASSERT_EQ(value.ValueInt(), 10);
  }
  {
    auto *condition_false =
        storage.Create<EqualOperator>(storage.Create<PrimitiveLiteral>(2),
                                      storage.Create<PrimitiveLiteral>(3));
    auto *op = storage.Create<IfOperator>(condition_false, then_expression,
                                          else_expression);
    auto value = op->Accept(eval);
    ASSERT_EQ(value.ValueInt(), 20);
  }
  {
    auto *condition_exception =
        storage.Create<AdditionOperator>(storage.Create<PrimitiveLiteral>(2),
                                         storage.Create<PrimitiveLiteral>(3));
    auto *op = storage.Create<IfOperator>(condition_exception, then_expression,
                                          else_expression);
    ASSERT_THROW(op->Accept(eval), QueryRuntimeException);
  }
}

TEST_F(ExpressionEvaluatorTest, NotOperator) {
  auto *op =
      storage.Create<NotOperator>(storage.Create<PrimitiveLiteral>(false));
  auto value = op->Accept(eval);
  ASSERT_EQ(value.ValueBool(), true);
}

TEST_F(ExpressionEvaluatorTest, UnaryPlusOperator) {
  auto *op =
      storage.Create<UnaryPlusOperator>(storage.Create<PrimitiveLiteral>(5));
  auto value = op->Accept(eval);
  ASSERT_EQ(value.ValueInt(), 5);
}

TEST_F(ExpressionEvaluatorTest, UnaryMinusOperator) {
  auto *op =
      storage.Create<UnaryMinusOperator>(storage.Create<PrimitiveLiteral>(5));
  auto value = op->Accept(eval);
  ASSERT_EQ(value.ValueInt(), -5);
}

TEST_F(ExpressionEvaluatorTest, IsNullOperator) {
  auto *op =
      storage.Create<IsNullOperator>(storage.Create<PrimitiveLiteral>(1));
  auto val1 = op->Accept(eval);
  ASSERT_EQ(val1.ValueBool(), false);
  op = storage.Create<IsNullOperator>(
      storage.Create<PrimitiveLiteral>(PropertyValue::Null));
  auto val2 = op->Accept(eval);
  ASSERT_EQ(val2.ValueBool(), true);
}

TEST_F(ExpressionEvaluatorTest, LabelsTest) {
  auto v1 = dba->InsertVertex();
  v1.add_label(dba->Label("ANIMAL"));
  v1.add_label(dba->Label("DOG"));
  v1.add_label(dba->Label("NICE_DOG"));
  auto *identifier = storage.Create<Identifier>("n");
  auto node_symbol = symbol_table.CreateSymbol("n", true);
  symbol_table[*identifier] = node_symbol;
  frame[node_symbol] = v1;
  {
    auto *op = storage.Create<LabelsTest>(
        identifier,
        std::vector<storage::Label>{dba->Label("DOG"), dba->Label("ANIMAL")});
    auto value = op->Accept(eval);
    EXPECT_EQ(value.ValueBool(), true);
  }
  {
    auto *op = storage.Create<LabelsTest>(
        identifier,
        std::vector<storage::Label>{dba->Label("DOG"), dba->Label("BAD_DOG"),
                                    dba->Label("ANIMAL")});
    auto value = op->Accept(eval);
    EXPECT_EQ(value.ValueBool(), false);
  }
  {
    frame[node_symbol] = TypedValue::Null;
    auto *op = storage.Create<LabelsTest>(
        identifier,
        std::vector<storage::Label>{dba->Label("DOG"), dba->Label("BAD_DOG"),
                                    dba->Label("ANIMAL")});
    auto value = op->Accept(eval);
    EXPECT_TRUE(value.IsNull());
  }
}

TEST_F(ExpressionEvaluatorTest, Aggregation) {
  auto aggr = storage.Create<Aggregation>(storage.Create<PrimitiveLiteral>(42),
                                          nullptr, Aggregation::Op::COUNT);
  auto aggr_sym = symbol_table.CreateSymbol("aggr", true);
  symbol_table[*aggr] = aggr_sym;
  frame[aggr_sym] = TypedValue(1);
  auto value = aggr->Accept(eval);
  EXPECT_EQ(value.ValueInt(), 1);
}

TEST_F(ExpressionEvaluatorTest, ListLiteral) {
  auto *list_literal = storage.Create<ListLiteral>(
      std::vector<Expression *>{storage.Create<PrimitiveLiteral>(1),
                                storage.Create<PrimitiveLiteral>("bla"),
                                storage.Create<PrimitiveLiteral>(true)});
  TypedValue result = list_literal->Accept(eval);
  ASSERT_TRUE(result.IsList());
  auto &result_elems = result.ValueList();
  ASSERT_EQ(3, result_elems.size());
  EXPECT_TRUE(result_elems[0].IsInt());
  ;
  EXPECT_TRUE(result_elems[1].IsString());
  ;
  EXPECT_TRUE(result_elems[2].IsBool());
  ;
}

TEST_F(ExpressionEvaluatorTest, ParameterLookup) {
  ctx.parameters.Add(0, 42);
  auto *param_lookup = storage.Create<ParameterLookup>(0);
  auto value = param_lookup->Accept(eval);
  ASSERT_TRUE(value.IsInt());
  EXPECT_EQ(value.ValueInt(), 42);
}

TEST_F(ExpressionEvaluatorTest, All) {
  AstStorage storage;
  auto *ident_x = IDENT("x");
  auto *all =
      ALL("x", LIST(LITERAL(1), LITERAL(2)), WHERE(EQ(ident_x, LITERAL(1))));
  const auto x_sym = symbol_table.CreateSymbol("x", true);
  symbol_table[*all->identifier_] = x_sym;
  symbol_table[*ident_x] = x_sym;
  auto value = all->Accept(eval);
  ASSERT_TRUE(value.IsBool());
  EXPECT_FALSE(value.ValueBool());
}

TEST_F(ExpressionEvaluatorTest, FunctionAllNullList) {
  AstStorage storage;
  auto *all = ALL("x", LITERAL(PropertyValue::Null), WHERE(LITERAL(true)));
  const auto x_sym = symbol_table.CreateSymbol("x", true);
  symbol_table[*all->identifier_] = x_sym;
  auto value = all->Accept(eval);
  EXPECT_TRUE(value.IsNull());
}

TEST_F(ExpressionEvaluatorTest, FunctionAllWhereWrongType) {
  AstStorage storage;
  auto *all = ALL("x", LIST(LITERAL(1)), WHERE(LITERAL(2)));
  const auto x_sym = symbol_table.CreateSymbol("x", true);
  symbol_table[*all->identifier_] = x_sym;
  EXPECT_THROW(all->Accept(eval), QueryRuntimeException);
}

TEST_F(ExpressionEvaluatorTest, FunctionSingle) {
  AstStorage storage;
  auto *ident_x = IDENT("x");
  auto *single =
      SINGLE("x", LIST(LITERAL(1), LITERAL(2)), WHERE(EQ(ident_x, LITERAL(1))));
  const auto x_sym = symbol_table.CreateSymbol("x", true);
  symbol_table[*single->identifier_] = x_sym;
  symbol_table[*ident_x] = x_sym;
  auto value = single->Accept(eval);
  ASSERT_TRUE(value.IsBool());
  EXPECT_TRUE(value.ValueBool());
}

TEST_F(ExpressionEvaluatorTest, FunctionSingle2) {
  AstStorage storage;
  auto *ident_x = IDENT("x");
  auto *single = SINGLE("x", LIST(LITERAL(1), LITERAL(2)),
                        WHERE(GREATER(ident_x, LITERAL(0))));
  const auto x_sym = symbol_table.CreateSymbol("x", true);
  symbol_table[*single->identifier_] = x_sym;
  symbol_table[*ident_x] = x_sym;
  auto value = single->Accept(eval);
  ASSERT_TRUE(value.IsBool());
  EXPECT_FALSE(value.ValueBool());
}

TEST_F(ExpressionEvaluatorTest, FunctionSingleNullList) {
  AstStorage storage;
  auto *single =
      SINGLE("x", LITERAL(PropertyValue::Null), WHERE(LITERAL(true)));
  const auto x_sym = symbol_table.CreateSymbol("x", true);
  symbol_table[*single->identifier_] = x_sym;
  auto value = single->Accept(eval);
  EXPECT_TRUE(value.IsNull());
}

TEST_F(ExpressionEvaluatorTest, FunctionReduce) {
  AstStorage storage;
  auto *ident_sum = IDENT("sum");
  auto *ident_x = IDENT("x");
  auto *reduce = REDUCE("sum", LITERAL(0), "x", LIST(LITERAL(1), LITERAL(2)),
                        ADD(ident_sum, ident_x));
  const auto sum_sym = symbol_table.CreateSymbol("sum", true);
  symbol_table[*reduce->accumulator_] = sum_sym;
  symbol_table[*ident_sum] = sum_sym;
  const auto x_sym = symbol_table.CreateSymbol("x", true);
  symbol_table[*reduce->identifier_] = x_sym;
  symbol_table[*ident_x] = x_sym;
  auto value = reduce->Accept(eval);
  ASSERT_TRUE(value.IsInt());
  EXPECT_EQ(value.ValueInt(), 3);
}

TEST_F(ExpressionEvaluatorTest, FunctionExtract) {
  AstStorage storage;
  auto *ident_x = IDENT("x");
  auto *extract =
      EXTRACT("x", LIST(LITERAL(1), LITERAL(2), LITERAL(PropertyValue::Null)),
              ADD(ident_x, LITERAL(1)));
  const auto x_sym = symbol_table.CreateSymbol("x", true);
  symbol_table[*extract->identifier_] = x_sym;
  symbol_table[*ident_x] = x_sym;
  auto value = extract->Accept(eval);
  EXPECT_TRUE(value.IsList());
  ;
  auto result = value.ValueList();
  EXPECT_EQ(result[0].ValueInt(), 2);
  EXPECT_EQ(result[1].ValueInt(), 3);
  EXPECT_TRUE(result[2].IsNull());
}

TEST_F(ExpressionEvaluatorTest, FunctionExtractNull) {
  AstStorage storage;
  auto *ident_x = IDENT("x");
  auto *extract =
      EXTRACT("x", LITERAL(PropertyValue::Null), ADD(ident_x, LITERAL(1)));
  const auto x_sym = symbol_table.CreateSymbol("x", true);
  symbol_table[*extract->identifier_] = x_sym;
  symbol_table[*ident_x] = x_sym;
  auto value = extract->Accept(eval);
  EXPECT_TRUE(value.IsNull());
}

TEST_F(ExpressionEvaluatorTest, FunctionExtractExceptions) {
  AstStorage storage;
  auto *ident_x = IDENT("x");
  auto *extract = EXTRACT("x", LITERAL("bla"), ADD(ident_x, LITERAL(1)));
  const auto x_sym = symbol_table.CreateSymbol("x", true);
  symbol_table[*extract->identifier_] = x_sym;
  symbol_table[*ident_x] = x_sym;
  EXPECT_THROW(extract->Accept(eval), QueryRuntimeException);
}

class ExpressionEvaluatorPropertyLookup : public ExpressionEvaluatorTest {
 protected:
  std::pair<std::string, storage::Property> prop_age =
      std::make_pair("age", dba->Property("age"));
  std::pair<std::string, storage::Property> prop_height =
      std::make_pair("height", dba->Property("height"));
  Expression *identifier = storage.Create<Identifier>("element");
  Symbol symbol = symbol_table.CreateSymbol("element", true);

  void SetUp() { symbol_table[*identifier] = symbol; }

  auto Value(std::pair<std::string, storage::Property> property) {
    auto *op = storage.Create<PropertyLookup>(identifier, property);
    return op->Accept(eval);
  }
};

TEST_F(ExpressionEvaluatorPropertyLookup, Vertex) {
  auto v1 = dba->InsertVertex();
  v1.PropsSet(prop_age.second, 10);
  frame[symbol] = v1;
  EXPECT_EQ(Value(prop_age).ValueInt(), 10);
  EXPECT_TRUE(Value(prop_height).IsNull());
}

TEST_F(ExpressionEvaluatorPropertyLookup, Edge) {
  auto v1 = dba->InsertVertex();
  auto v2 = dba->InsertVertex();
  auto e12 = dba->InsertEdge(v1, v2, dba->EdgeType("edge_type"));
  e12.PropsSet(prop_age.second, 10);
  frame[symbol] = e12;
  EXPECT_EQ(Value(prop_age).ValueInt(), 10);
  EXPECT_TRUE(Value(prop_height).IsNull());
}

TEST_F(ExpressionEvaluatorPropertyLookup, Null) {
  frame[symbol] = TypedValue::Null;
  EXPECT_TRUE(Value(prop_age).IsNull());
}

TEST_F(ExpressionEvaluatorPropertyLookup, MapLiteral) {
  frame[symbol] = std::map<std::string, TypedValue>{{prop_age.first, 10}};
  EXPECT_EQ(Value(prop_age).ValueInt(), 10);
  EXPECT_TRUE(Value(prop_height).IsNull());
}

class FunctionTest : public ExpressionEvaluatorTest {
 protected:
  TypedValue EvaluateFunction(const std::string &function_name,
                              const std::vector<TypedValue> &args) {
    std::vector<Expression *> expressions;
    for (size_t i = 0; i < args.size(); ++i) {
      auto *ident =
          storage.Create<Identifier>("arg_" + std::to_string(i), true);
      auto sym = symbol_table.CreateSymbol("arg_" + std::to_string(i), true);
      symbol_table[*ident] = sym;
      frame[sym] = args[i];
      expressions.push_back(ident);
    }
    auto *op = storage.Create<Function>(function_name, expressions);
    return op->Accept(eval);
  }
};

TEST_F(FunctionTest, Coalesce) {
  ASSERT_THROW(EvaluateFunction("COALESCE", {}), QueryRuntimeException);
  ASSERT_TRUE(EvaluateFunction("COALESCE", {TypedValue::Null, TypedValue::Null})
                  .IsNull());
  ASSERT_EQ(EvaluateFunction("COALESCE", {TypedValue::Null, 2, 3}).ValueInt(),
            2);
}

TEST_F(FunctionTest, EndNode) {
  ASSERT_THROW(EvaluateFunction("ENDNODE", {}), QueryRuntimeException);
  ASSERT_TRUE(EvaluateFunction("ENDNODE", {TypedValue::Null}).IsNull());
  auto v1 = dba->InsertVertex();
  v1.add_label(dba->Label("label1"));
  auto v2 = dba->InsertVertex();
  v2.add_label(dba->Label("label2"));
  auto e = dba->InsertEdge(v1, v2, dba->EdgeType("t"));
  ASSERT_TRUE(EvaluateFunction("ENDNODE", {e})
                  .ValueVertex()
                  .has_label(dba->Label("label2")));
  ASSERT_THROW(EvaluateFunction("ENDNODE", {2}), QueryRuntimeException);
}

TEST_F(FunctionTest, Head) {
  ASSERT_THROW(EvaluateFunction("HEAD", {}), QueryRuntimeException);
  ASSERT_TRUE(EvaluateFunction("HEAD", {TypedValue::Null}).IsNull());
  std::vector<TypedValue> arguments;
  arguments.push_back(std::vector<TypedValue>{3, 4, 5});
  ASSERT_EQ(EvaluateFunction("HEAD", arguments).ValueInt(), 3);
  arguments[0].ValueList().clear();
  ASSERT_TRUE(EvaluateFunction("HEAD", arguments).IsNull());
  ASSERT_THROW(EvaluateFunction("HEAD", {2}), QueryRuntimeException);
}

TEST_F(FunctionTest, Properties) {
  ASSERT_THROW(EvaluateFunction("PROPERTIES", {}), QueryRuntimeException);
  ASSERT_TRUE(EvaluateFunction("PROPERTIES", {TypedValue::Null}).IsNull());
  auto v1 = dba->InsertVertex();
  v1.PropsSet(dba->Property("height"), 5);
  v1.PropsSet(dba->Property("age"), 10);
  auto v2 = dba->InsertVertex();
  auto e = dba->InsertEdge(v1, v2, dba->EdgeType("type1"));
  e.PropsSet(dba->Property("height"), 3);
  e.PropsSet(dba->Property("age"), 15);

  auto prop_values_to_int = [](TypedValue t) {
    std::unordered_map<std::string, int> properties;
    for (auto property : t.Value<std::map<std::string, TypedValue>>()) {
      properties[property.first] = property.second.ValueInt();
    }
    return properties;
  };

  ASSERT_THAT(prop_values_to_int(EvaluateFunction("PROPERTIES", {v1})),
              UnorderedElementsAre(testing::Pair("height", 5),
                                   testing::Pair("age", 10)));
  ASSERT_THAT(prop_values_to_int(EvaluateFunction("PROPERTIES", {e})),
              UnorderedElementsAre(testing::Pair("height", 3),
                                   testing::Pair("age", 15)));
  ASSERT_THROW(EvaluateFunction("PROPERTIES", {2}), QueryRuntimeException);
}

TEST_F(FunctionTest, Last) {
  ASSERT_THROW(EvaluateFunction("LAST", {}), QueryRuntimeException);
  ASSERT_TRUE(EvaluateFunction("LAST", {TypedValue::Null}).IsNull());
  std::vector<TypedValue> arguments;
  arguments.push_back(std::vector<TypedValue>{3, 4, 5});
  ASSERT_EQ(EvaluateFunction("LAST", arguments).ValueInt(), 5);
  arguments[0].ValueList().clear();
  ASSERT_TRUE(EvaluateFunction("LAST", arguments).IsNull());
  ASSERT_THROW(EvaluateFunction("LAST", {5}), QueryRuntimeException);
}

TEST_F(FunctionTest, Size) {
  ASSERT_THROW(EvaluateFunction("SIZE", {}), QueryRuntimeException);
  ASSERT_TRUE(EvaluateFunction("SIZE", {TypedValue::Null}).IsNull());
  std::vector<TypedValue> arguments;
  arguments.push_back(std::vector<TypedValue>{3, 4, 5});
  ASSERT_EQ(EvaluateFunction("SIZE", arguments).ValueInt(), 3);
  ASSERT_EQ(EvaluateFunction("SIZE", {"john"}).ValueInt(), 4);
  ASSERT_EQ(EvaluateFunction("SIZE", {std::map<std::string, TypedValue>{
                                         {"a", 5}, {"b", true}, {"c", "123"}}})
                .ValueInt(),
            3);
  ASSERT_THROW(EvaluateFunction("SIZE", {5}), QueryRuntimeException);

  auto v0 = dba->InsertVertex();
  query::Path path(v0);
  EXPECT_EQ(EvaluateFunction("SIZE", {path}).ValueInt(), 0);
  auto v1 = dba->InsertVertex();
  path.Expand(dba->InsertEdge(v0, v1, dba->EdgeType("type")));
  path.Expand(v1);
  EXPECT_EQ(EvaluateFunction("SIZE", {path}).ValueInt(), 1);
}

TEST_F(FunctionTest, StartNode) {
  ASSERT_THROW(EvaluateFunction("STARTNODE", {}), QueryRuntimeException);
  ASSERT_TRUE(EvaluateFunction("STARTNODE", {TypedValue::Null}).IsNull());
  auto v1 = dba->InsertVertex();
  v1.add_label(dba->Label("label1"));
  auto v2 = dba->InsertVertex();
  v2.add_label(dba->Label("label2"));
  auto e = dba->InsertEdge(v1, v2, dba->EdgeType("t"));
  ASSERT_TRUE(EvaluateFunction("STARTNODE", {e})
                  .ValueVertex()
                  .has_label(dba->Label("label1")));
  ASSERT_THROW(EvaluateFunction("STARTNODE", {2}), QueryRuntimeException);
}

TEST_F(FunctionTest, Degree) {
  ASSERT_THROW(EvaluateFunction("DEGREE", {}), QueryRuntimeException);
  ASSERT_TRUE(EvaluateFunction("DEGREE", {TypedValue::Null}).IsNull());
  auto v1 = dba->InsertVertex();
  auto v2 = dba->InsertVertex();
  auto v3 = dba->InsertVertex();
  auto e12 = dba->InsertEdge(v1, v2, dba->EdgeType("t"));
  dba->InsertEdge(v3, v2, dba->EdgeType("t"));
  ASSERT_EQ(EvaluateFunction("DEGREE", {v1}).ValueInt(), 1);
  ASSERT_EQ(EvaluateFunction("DEGREE", {v2}).ValueInt(), 2);
  ASSERT_EQ(EvaluateFunction("DEGREE", {v3}).ValueInt(), 1);
  ASSERT_THROW(EvaluateFunction("DEGREE", {2}), QueryRuntimeException);
  ASSERT_THROW(EvaluateFunction("DEGREE", {e12}), QueryRuntimeException);
}

TEST_F(FunctionTest, ToBoolean) {
  ASSERT_THROW(EvaluateFunction("TOBOOLEAN", {}), QueryRuntimeException);
  ASSERT_TRUE(EvaluateFunction("TOBOOLEAN", {TypedValue::Null}).IsNull());
  ASSERT_EQ(EvaluateFunction("TOBOOLEAN", {123}).ValueBool(), true);
  ASSERT_EQ(EvaluateFunction("TOBOOLEAN", {-213}).ValueBool(), true);
  ASSERT_EQ(EvaluateFunction("TOBOOLEAN", {0}).ValueBool(), false);
  ASSERT_EQ(EvaluateFunction("TOBOOLEAN", {" trUE \n\t"}).ValueBool(), true);
  ASSERT_EQ(EvaluateFunction("TOBOOLEAN", {"\n\tFalsE"}).ValueBool(), false);
  ASSERT_TRUE(EvaluateFunction("TOBOOLEAN", {"\n\tFALSEA "}).IsNull());
  ASSERT_EQ(EvaluateFunction("TOBOOLEAN", {true}).ValueBool(), true);
  ASSERT_EQ(EvaluateFunction("TOBOOLEAN", {false}).ValueBool(), false);
}

TEST_F(FunctionTest, ToFloat) {
  ASSERT_THROW(EvaluateFunction("TOFLOAT", {}), QueryRuntimeException);
  ASSERT_TRUE(EvaluateFunction("TOFLOAT", {TypedValue::Null}).IsNull());
  ASSERT_EQ(EvaluateFunction("TOFLOAT", {" -3.5 \n\t"}).ValueDouble(), -3.5);
  ASSERT_EQ(EvaluateFunction("TOFLOAT", {"\n\t0.5e-1"}).ValueDouble(), 0.05);
  ASSERT_TRUE(EvaluateFunction("TOFLOAT", {"\n\t3.4e-3X "}).IsNull());
  ASSERT_EQ(EvaluateFunction("TOFLOAT", {-3.5}).ValueDouble(), -3.5);
  ASSERT_EQ(EvaluateFunction("TOFLOAT", {-3}).ValueDouble(), -3.0);
  ASSERT_THROW(EvaluateFunction("TOFLOAT", {true}), QueryRuntimeException);
}

TEST_F(FunctionTest, ToInteger) {
  ASSERT_THROW(EvaluateFunction("TOINTEGER", {}), QueryRuntimeException);
  ASSERT_TRUE(EvaluateFunction("TOINTEGER", {TypedValue::Null}).IsNull());
  ASSERT_EQ(EvaluateFunction("TOINTEGER", {false}).ValueInt(), 0);
  ASSERT_EQ(EvaluateFunction("TOINTEGER", {true}).ValueInt(), 1);
  ASSERT_EQ(EvaluateFunction("TOINTEGER", {"\n\t3"}).ValueInt(), 3);
  ASSERT_EQ(EvaluateFunction("TOINTEGER", {" -3.5 \n\t"}).ValueInt(), -3);
  ASSERT_TRUE(EvaluateFunction("TOINTEGER", {"\n\t3X "}).IsNull());
  ASSERT_EQ(EvaluateFunction("TOINTEGER", {-3.5}).ValueInt(), -3);
  ASSERT_EQ(EvaluateFunction("TOINTEGER", {3.5}).ValueInt(), 3);
}

TEST_F(FunctionTest, Type) {
  ASSERT_THROW(EvaluateFunction("TYPE", {}), QueryRuntimeException);
  ASSERT_TRUE(EvaluateFunction("TYPE", {TypedValue::Null}).IsNull());
  auto v1 = dba->InsertVertex();
  v1.add_label(dba->Label("label1"));
  auto v2 = dba->InsertVertex();
  v2.add_label(dba->Label("label2"));
  auto e = dba->InsertEdge(v1, v2, dba->EdgeType("type1"));
  ASSERT_EQ(EvaluateFunction("TYPE", {e}).ValueString(), "type1");
  ASSERT_THROW(EvaluateFunction("TYPE", {2}), QueryRuntimeException);
}

TEST_F(FunctionTest, Labels) {
  ASSERT_THROW(EvaluateFunction("LABELS", {}), QueryRuntimeException);
  ASSERT_TRUE(EvaluateFunction("LABELS", {TypedValue::Null}).IsNull());
  auto v = dba->InsertVertex();
  v.add_label(dba->Label("label1"));
  v.add_label(dba->Label("label2"));
  std::vector<std::string> labels;
  auto _labels = EvaluateFunction("LABELS", {v}).ValueList();
  for (auto label : _labels) {
    labels.push_back(label.ValueString());
  }
  ASSERT_THAT(labels, UnorderedElementsAre("label1", "label2"));
  ASSERT_THROW(EvaluateFunction("LABELS", {2}), QueryRuntimeException);
}

TEST_F(FunctionTest, NodesRelationships) {
  EXPECT_THROW(EvaluateFunction("NODES", {}), QueryRuntimeException);
  EXPECT_THROW(EvaluateFunction("RELATIONSHIPS", {}), QueryRuntimeException);
  EXPECT_TRUE(EvaluateFunction("NODES", {TypedValue::Null}).IsNull());
  EXPECT_TRUE(EvaluateFunction("RELATIONSHIPS", {TypedValue::Null}).IsNull());

  {
    auto v1 = dba->InsertVertex();
    auto v2 = dba->InsertVertex();
    auto v3 = dba->InsertVertex();
    auto e1 = dba->InsertEdge(v1, v2, dba->EdgeType("Type"));
    auto e2 = dba->InsertEdge(v2, v3, dba->EdgeType("Type"));
    query::Path path(v1, e1, v2, e2, v3);

    auto _nodes = EvaluateFunction("NODES", {path}).ValueList();
    std::vector<VertexAccessor> nodes;
    for (const auto &node : _nodes) {
      nodes.push_back(node.ValueVertex());
    }
    EXPECT_THAT(nodes, ElementsAre(v1, v2, v3));

    auto _edges = EvaluateFunction("RELATIONSHIPS", {path}).ValueList();
    std::vector<EdgeAccessor> edges;
    for (const auto &edge : _edges) {
      edges.push_back(edge.ValueEdge());
    }
    EXPECT_THAT(edges, ElementsAre(e1, e2));
  }

  EXPECT_THROW(EvaluateFunction("NODES", {2}), QueryRuntimeException);
  EXPECT_THROW(EvaluateFunction("RELATIONSHIPS", {2}), QueryRuntimeException);
}

TEST_F(FunctionTest, Range) {
  EXPECT_THROW(EvaluateFunction("RANGE", {}), QueryRuntimeException);
  EXPECT_TRUE(EvaluateFunction("RANGE", {1, 2, TypedValue::Null}).IsNull());
  EXPECT_THROW(EvaluateFunction("RANGE", {1, TypedValue::Null, 1.3}),
               QueryRuntimeException);
  EXPECT_THROW(EvaluateFunction("RANGE", {1, 2, 0}), QueryRuntimeException);
  EXPECT_THAT(ToList<int64_t>(EvaluateFunction("RANGE", {1, 3})),
              ElementsAre(1, 2, 3));
  EXPECT_THAT(ToList<int64_t>(EvaluateFunction("RANGE", {-1, 5, 2})),
              ElementsAre(-1, 1, 3, 5));
  EXPECT_THAT(ToList<int64_t>(EvaluateFunction("RANGE", {2, 10, 3})),
              ElementsAre(2, 5, 8));
  EXPECT_THAT(ToList<int64_t>(EvaluateFunction("RANGE", {2, 2, 2})),
              ElementsAre(2));
  EXPECT_THAT(ToList<int64_t>(EvaluateFunction("RANGE", {3, 0, 5})),
              ElementsAre());
  EXPECT_THAT(ToList<int64_t>(EvaluateFunction("RANGE", {5, 1, -2})),
              ElementsAre(5, 3, 1));
  EXPECT_THAT(ToList<int64_t>(EvaluateFunction("RANGE", {6, 1, -2})),
              ElementsAre(6, 4, 2));
  EXPECT_THAT(ToList<int64_t>(EvaluateFunction("RANGE", {2, 2, -3})),
              ElementsAre(2));
  EXPECT_THAT(ToList<int64_t>(EvaluateFunction("RANGE", {-2, 4, -1})),
              ElementsAre());
}

TEST_F(FunctionTest, Keys) {
  ASSERT_THROW(EvaluateFunction("KEYS", {}), QueryRuntimeException);
  ASSERT_TRUE(EvaluateFunction("KEYS", {TypedValue::Null}).IsNull());
  auto v1 = dba->InsertVertex();
  v1.PropsSet(dba->Property("height"), 5);
  v1.PropsSet(dba->Property("age"), 10);
  auto v2 = dba->InsertVertex();
  auto e = dba->InsertEdge(v1, v2, dba->EdgeType("type1"));
  e.PropsSet(dba->Property("width"), 3);
  e.PropsSet(dba->Property("age"), 15);

  auto prop_keys_to_string = [](TypedValue t) {
    std::vector<std::string> keys;
    for (auto property : t.ValueList()) {
      keys.push_back(property.ValueString());
    }
    return keys;
  };
  ASSERT_THAT(prop_keys_to_string(EvaluateFunction("KEYS", {v1})),
              UnorderedElementsAre("height", "age"));
  ASSERT_THAT(prop_keys_to_string(EvaluateFunction("KEYS", {e})),
              UnorderedElementsAre("width", "age"));
  ASSERT_THROW(EvaluateFunction("KEYS", {2}), QueryRuntimeException);
}

TEST_F(FunctionTest, Tail) {
  ASSERT_THROW(EvaluateFunction("TAIL", {}), QueryRuntimeException);
  ASSERT_TRUE(EvaluateFunction("TAIL", {TypedValue::Null}).IsNull());
  std::vector<TypedValue> arguments;
  arguments.push_back(std::vector<TypedValue>{});
  ASSERT_EQ(EvaluateFunction("TAIL", arguments).ValueList().size(), 0U);
  arguments[0] = std::vector<TypedValue>{3, 4, true, "john"};
  auto list = EvaluateFunction("TAIL", arguments).ValueList();
  ASSERT_EQ(list.size(), 3U);
  ASSERT_EQ(list[0].ValueInt(), 4);
  ASSERT_EQ(list[1].ValueBool(), true);
  ASSERT_EQ(list[2].ValueString(), "john");
  ASSERT_THROW(EvaluateFunction("TAIL", {2}), QueryRuntimeException);
}

TEST_F(FunctionTest, Abs) {
  ASSERT_THROW(EvaluateFunction("ABS", {}), QueryRuntimeException);
  ASSERT_TRUE(EvaluateFunction("ABS", {TypedValue::Null}).IsNull());
  ASSERT_EQ(EvaluateFunction("ABS", {-2}).ValueInt(), 2);
  ASSERT_EQ(EvaluateFunction("ABS", {-2.5}).ValueDouble(), 2.5);
  ASSERT_THROW(EvaluateFunction("ABS", {true}), QueryRuntimeException);
}

// Test if log works. If it does then all functions wrapped with
// WRAP_CMATH_FLOAT_FUNCTION macro should work and are not gonna be tested for
// correctnes..
TEST_F(FunctionTest, Log) {
  ASSERT_THROW(EvaluateFunction("LOG", {}), QueryRuntimeException);
  ASSERT_TRUE(EvaluateFunction("LOG", {TypedValue::Null}).IsNull());
  ASSERT_DOUBLE_EQ(EvaluateFunction("LOG", {2}).ValueDouble(), log(2));
  ASSERT_DOUBLE_EQ(EvaluateFunction("LOG", {1.5}).ValueDouble(), log(1.5));
  // Not portable, but should work on most platforms.
  ASSERT_TRUE(std::isnan(EvaluateFunction("LOG", {-1.5}).ValueDouble()));
  ASSERT_THROW(EvaluateFunction("LOG", {true}), QueryRuntimeException);
}

// Function Round wraps round from cmath and will work if FunctionTest.Log test
// passes. This test is used to show behavior of round since it differs from
// neo4j's round.
TEST_F(FunctionTest, Round) {
  ASSERT_THROW(EvaluateFunction("ROUND", {}), QueryRuntimeException);
  ASSERT_TRUE(EvaluateFunction("ROUND", {TypedValue::Null}).IsNull());
  ASSERT_EQ(EvaluateFunction("ROUND", {-2}).ValueDouble(), -2);
  ASSERT_EQ(EvaluateFunction("ROUND", {-2.4}).ValueDouble(), -2);
  ASSERT_EQ(EvaluateFunction("ROUND", {-2.5}).ValueDouble(), -3);
  ASSERT_EQ(EvaluateFunction("ROUND", {-2.6}).ValueDouble(), -3);
  ASSERT_EQ(EvaluateFunction("ROUND", {2.4}).ValueDouble(), 2);
  ASSERT_EQ(EvaluateFunction("ROUND", {2.5}).ValueDouble(), 3);
  ASSERT_EQ(EvaluateFunction("ROUND", {2.6}).ValueDouble(), 3);
  ASSERT_THROW(EvaluateFunction("ROUND", {true}), QueryRuntimeException);
}

// Check if wrapped functions are callable (check if everything was spelled
// correctly...). Wrapper correctnes is checked in FunctionTest.Log function
// test.
TEST_F(FunctionTest, WrappedMathFunctions) {
  for (auto function_name :
       {"FLOOR", "CEIL", "ROUND", "EXP", "LOG", "LOG10", "SQRT", "ACOS", "ASIN",
        "ATAN", "COS", "SIN", "TAN"}) {
    EvaluateFunction(function_name, {0.5});
  }
}

TEST_F(FunctionTest, Atan2) {
  ASSERT_THROW(EvaluateFunction("ATAN2", {}), QueryRuntimeException);
  ASSERT_TRUE(EvaluateFunction("ATAN2", {TypedValue::Null, 1}).IsNull());
  ASSERT_TRUE(EvaluateFunction("ATAN2", {1, TypedValue::Null}).IsNull());
  ASSERT_DOUBLE_EQ(EvaluateFunction("ATAN2", {2, -1.0}).ValueDouble(),
                   atan2(2, -1));
  ASSERT_THROW(EvaluateFunction("ATAN2", {3.0, true}), QueryRuntimeException);
}

TEST_F(FunctionTest, Sign) {
  ASSERT_THROW(EvaluateFunction("SIGN", {}), QueryRuntimeException);
  ASSERT_TRUE(EvaluateFunction("SIGN", {TypedValue::Null}).IsNull());
  ASSERT_EQ(EvaluateFunction("SIGN", {-2}).ValueInt(), -1);
  ASSERT_EQ(EvaluateFunction("SIGN", {-0.2}).ValueInt(), -1);
  ASSERT_EQ(EvaluateFunction("SIGN", {0.0}).ValueInt(), 0);
  ASSERT_EQ(EvaluateFunction("SIGN", {2.5}).ValueInt(), 1);
  ASSERT_THROW(EvaluateFunction("SIGN", {true}), QueryRuntimeException);
}

TEST_F(FunctionTest, E) {
  ASSERT_THROW(EvaluateFunction("E", {1}), QueryRuntimeException);
  ASSERT_DOUBLE_EQ(EvaluateFunction("E", {}).ValueDouble(), M_E);
}

TEST_F(FunctionTest, Pi) {
  ASSERT_THROW(EvaluateFunction("PI", {1}), QueryRuntimeException);
  ASSERT_DOUBLE_EQ(EvaluateFunction("PI", {}).ValueDouble(), M_PI);
}

TEST_F(FunctionTest, Rand) {
  ASSERT_THROW(EvaluateFunction("RAND", {1}), QueryRuntimeException);
  ASSERT_GE(EvaluateFunction("RAND", {}).ValueDouble(), 0.0);
  ASSERT_LT(EvaluateFunction("RAND", {}).ValueDouble(), 1.0);
}

TEST_F(FunctionTest, StartsWith) {
  EXPECT_THROW(EvaluateFunction(kStartsWith, {}), QueryRuntimeException);
  EXPECT_TRUE(EvaluateFunction(kStartsWith, {"a", TypedValue::Null}).IsNull());
  EXPECT_THROW(EvaluateFunction(kStartsWith, {TypedValue::Null, 1.3}),
               QueryRuntimeException);
  EXPECT_TRUE(EvaluateFunction(kStartsWith, {"abc", "abc"}).ValueBool());
  EXPECT_TRUE(EvaluateFunction(kStartsWith, {"abcdef", "abc"}).ValueBool());
  EXPECT_FALSE(EvaluateFunction(kStartsWith, {"abcdef", "aBc"}).ValueBool());
  EXPECT_FALSE(EvaluateFunction(kStartsWith, {"abc", "abcd"}).ValueBool());
}

TEST_F(FunctionTest, EndsWith) {
  EXPECT_THROW(EvaluateFunction(kEndsWith, {}), QueryRuntimeException);
  EXPECT_TRUE(EvaluateFunction(kEndsWith, {"a", TypedValue::Null}).IsNull());
  EXPECT_THROW(EvaluateFunction(kEndsWith, {TypedValue::Null, 1.3}),
               QueryRuntimeException);
  EXPECT_TRUE(EvaluateFunction(kEndsWith, {"abc", "abc"}).ValueBool());
  EXPECT_TRUE(EvaluateFunction(kEndsWith, {"abcdef", "def"}).ValueBool());
  EXPECT_FALSE(EvaluateFunction(kEndsWith, {"abcdef", "dEf"}).ValueBool());
  EXPECT_FALSE(EvaluateFunction(kEndsWith, {"bcd", "abcd"}).ValueBool());
}

TEST_F(FunctionTest, Contains) {
  EXPECT_THROW(EvaluateFunction(kContains, {}), QueryRuntimeException);
  EXPECT_TRUE(EvaluateFunction(kContains, {"a", TypedValue::Null}).IsNull());
  EXPECT_THROW(EvaluateFunction(kContains, {TypedValue::Null, 1.3}),
               QueryRuntimeException);
  EXPECT_TRUE(EvaluateFunction(kContains, {"abc", "abc"}).ValueBool());
  EXPECT_TRUE(EvaluateFunction(kContains, {"abcde", "bcd"}).ValueBool());
  EXPECT_FALSE(EvaluateFunction(kContains, {"cde", "abcdef"}).ValueBool());
  EXPECT_FALSE(EvaluateFunction(kContains, {"abcdef", "dEf"}).ValueBool());
}

TEST_F(FunctionTest, Assert) {
  // Invalid calls.
  ASSERT_THROW(EvaluateFunction("ASSERT", {}), QueryRuntimeException);
  ASSERT_THROW(EvaluateFunction("ASSERT", {false, false}),
               QueryRuntimeException);
  ASSERT_THROW(EvaluateFunction("ASSERT", {"string", false}),
               QueryRuntimeException);
  ASSERT_THROW(EvaluateFunction("ASSERT", {false, "reason", true}),
               QueryRuntimeException);

  // Valid calls, assertion fails.
  ASSERT_THROW(EvaluateFunction("ASSERT", {false}), QueryRuntimeException);
  ASSERT_THROW(EvaluateFunction("ASSERT", {false, "message"}),
               QueryRuntimeException);
  try {
    EvaluateFunction("ASSERT", {false, "bbgba"});
  } catch (QueryRuntimeException &e) {
    ASSERT_TRUE(std::string(e.what()).find("bbgba") != std::string::npos);
  }

  // Valid calls, assertion passes.
  ASSERT_TRUE(EvaluateFunction("ASSERT", {true}).ValueBool());
  ASSERT_TRUE(EvaluateFunction("ASSERT", {true, "message"}).ValueBool());
}

TEST_F(FunctionTest, Counter) {
  EXPECT_THROW(EvaluateFunction("COUNTER", {}), QueryRuntimeException);
  EXPECT_THROW(EvaluateFunction("COUNTER", {"a", "b"}), QueryRuntimeException);
  EXPECT_EQ(EvaluateFunction("COUNTER", {"c1"}).ValueInt(), 0);
  EXPECT_EQ(EvaluateFunction("COUNTER", {"c1"}).ValueInt(), 1);
  EXPECT_EQ(EvaluateFunction("COUNTER", {"c2"}).ValueInt(), 0);
  EXPECT_EQ(EvaluateFunction("COUNTER", {"c1"}).ValueInt(), 2);
  EXPECT_EQ(EvaluateFunction("COUNTER", {"c2"}).ValueInt(), 1);
}

TEST_F(FunctionTest, CounterSet) {
  EXPECT_THROW(EvaluateFunction("COUNTERSET", {}), QueryRuntimeException);
  EXPECT_THROW(EvaluateFunction("COUNTERSET", {"a"}), QueryRuntimeException);
  EXPECT_THROW(EvaluateFunction("COUNTERSET", {"a", "b"}),
               QueryRuntimeException);
  EXPECT_THROW(EvaluateFunction("COUNTERSET", {"a", 11, 12}),
               QueryRuntimeException);
  EXPECT_EQ(EvaluateFunction("COUNTER", {"c1"}).ValueInt(), 0);
  EvaluateFunction("COUNTERSET", {"c1", 12});
  EXPECT_EQ(EvaluateFunction("COUNTER", {"c1"}).ValueInt(), 12);
  EvaluateFunction("COUNTERSET", {"c2", 42});
  EXPECT_EQ(EvaluateFunction("COUNTER", {"c2"}).ValueInt(), 42);
  EXPECT_EQ(EvaluateFunction("COUNTER", {"c1"}).ValueInt(), 13);
  EXPECT_EQ(EvaluateFunction("COUNTER", {"c2"}).ValueInt(), 43);
}

TEST_F(FunctionTest, IndexInfo) {
  EXPECT_THROW(EvaluateFunction("INDEXINFO", {1}), QueryRuntimeException);
  EXPECT_EQ(EvaluateFunction("INDEXINFO", {}).ValueList().size(), 0);
  dba->InsertVertex().add_label(dba->Label("l1"));
  {
    auto info = ToList<std::string>(EvaluateFunction("INDEXINFO", {}));
    EXPECT_EQ(info.size(), 1);
    EXPECT_EQ(info[0], ":l1");
  }
  {
    dba->BuildIndex(dba->Label("l1"), dba->Property("prop"));
    auto info = ToList<std::string>(EvaluateFunction("INDEXINFO", {}));
    EXPECT_EQ(info.size(), 2);
    EXPECT_THAT(info, testing::UnorderedElementsAre(":l1", ":l1(prop)"));
  }
}

TEST_F(FunctionTest, Id) {
  auto va = dba->InsertVertex();
  auto ea = dba->InsertEdge(va, va, dba->EdgeType("edge"));
  auto vb = dba->InsertVertex();
  EXPECT_EQ(EvaluateFunction("ID", {va}).ValueInt(), 0);
  EXPECT_EQ(EvaluateFunction("ID", {ea}).ValueInt(), 0);
  EXPECT_EQ(EvaluateFunction("ID", {vb}).ValueInt(), 1024);
  EXPECT_THROW(EvaluateFunction("ID", {}), QueryRuntimeException);
  EXPECT_THROW(EvaluateFunction("ID", {0}), QueryRuntimeException);
  EXPECT_THROW(EvaluateFunction("ID", {va, ea}), QueryRuntimeException);
}

TEST_F(FunctionTest, WorkerIdException) {
  auto va = dba->InsertVertex();
  EXPECT_THROW(EvaluateFunction("WORKERID", {}), QueryRuntimeException);
  EXPECT_THROW(EvaluateFunction("WORKERID", {va, va}), QueryRuntimeException);
}

TEST_F(FunctionTest, WorkerIdSingleNode) {
  auto va = dba->InsertVertex();
  EXPECT_EQ(EvaluateFunction("WORKERID", {va}).ValueInt(), 0);
}

TEST_F(FunctionTest, ToStringNull) {
  EXPECT_TRUE(EvaluateFunction("TOSTRING", {TypedValue::Null}).IsNull());
}

TEST_F(FunctionTest, ToStringString) {
  EXPECT_EQ(EvaluateFunction("TOSTRING", {""}).ValueString(), "");
  EXPECT_EQ(EvaluateFunction("TOSTRING", {"this is a string"}).ValueString(),
            "this is a string");
}

TEST_F(FunctionTest, ToStringInteger) {
  EXPECT_EQ(EvaluateFunction("TOSTRING", {-23321312}).ValueString(),
            "-23321312");
  EXPECT_EQ(EvaluateFunction("TOSTRING", {0}).ValueString(), "0");
  EXPECT_EQ(EvaluateFunction("TOSTRING", {42}).ValueString(), "42");
}

TEST_F(FunctionTest, ToStringDouble) {
  EXPECT_EQ(EvaluateFunction("TOSTRING", {-42.42}).ValueString(), "-42.420000");
  EXPECT_EQ(EvaluateFunction("TOSTRING", {0.0}).ValueString(), "0.000000");
  EXPECT_EQ(EvaluateFunction("TOSTRING", {238910.2313217}).ValueString(),
            "238910.231322");
}

TEST_F(FunctionTest, ToStringBool) {
  EXPECT_EQ(EvaluateFunction("TOSTRING", {true}).ValueString(), "true");
  EXPECT_EQ(EvaluateFunction("TOSTRING", {false}).ValueString(), "false");
}

TEST_F(FunctionTest, ToStringExceptions) {
  EXPECT_THROW(EvaluateFunction("TOSTRING", {1, 2, 3}), QueryRuntimeException);
  std::vector<TypedValue> l{1, 2, 3};
  EXPECT_THROW(EvaluateFunction("TOSTRING", l), QueryRuntimeException);
}

TEST_F(FunctionTest, Timestamp) {
  ctx.timestamp = 42;
  EXPECT_EQ(EvaluateFunction("TIMESTAMP", {}).ValueInt(), 42);
}

TEST_F(FunctionTest, TimestampExceptions) {
  ctx.timestamp = 42;
  EXPECT_THROW(EvaluateFunction("TIMESTAMP", {1}).ValueInt(),
               QueryRuntimeException);
}

TEST_F(FunctionTest, Left) {
  EXPECT_THROW(EvaluateFunction("LEFT", {}), QueryRuntimeException);

  EXPECT_TRUE(
      EvaluateFunction("LEFT", {TypedValue::Null, TypedValue::Null}).IsNull());
  EXPECT_TRUE(EvaluateFunction("LEFT", {TypedValue::Null, 10}).IsNull());
  EXPECT_THROW(EvaluateFunction("LEFT", {TypedValue::Null, -10}),
               QueryRuntimeException);

  EXPECT_EQ(EvaluateFunction("LEFT", {"memgraph", 0}).ValueString(), "");
  EXPECT_EQ(EvaluateFunction("LEFT", {"memgraph", 3}).ValueString(), "mem");
  EXPECT_EQ(EvaluateFunction("LEFT", {"memgraph", 1000}).ValueString(),
            "memgraph");
  EXPECT_THROW(EvaluateFunction("LEFT", {"memgraph", -10}),
               QueryRuntimeException);
  EXPECT_THROW(EvaluateFunction("LEFT", {"memgraph", "graph"}),
               QueryRuntimeException);

  EXPECT_THROW(EvaluateFunction("LEFT", {132, 10}), QueryRuntimeException);
}

TEST_F(FunctionTest, Right) {
  EXPECT_THROW(EvaluateFunction("RIGHT", {}), QueryRuntimeException);

  EXPECT_TRUE(
      EvaluateFunction("RIGHT", {TypedValue::Null, TypedValue::Null}).IsNull());
  EXPECT_TRUE(EvaluateFunction("RIGHT", {TypedValue::Null, 10}).IsNull());
  EXPECT_THROW(EvaluateFunction("RIGHT", {TypedValue::Null, -10}),
               QueryRuntimeException);

  EXPECT_EQ(EvaluateFunction("RIGHT", {"memgraph", 0}).ValueString(), "");
  EXPECT_EQ(EvaluateFunction("RIGHT", {"memgraph", 3}).ValueString(), "aph");
  EXPECT_EQ(EvaluateFunction("RIGHT", {"memgraph", 1000}).ValueString(),
            "memgraph");
  EXPECT_THROW(EvaluateFunction("RIGHT", {"memgraph", -10}),
               QueryRuntimeException);
  EXPECT_THROW(EvaluateFunction("RIGHT", {"memgraph", "graph"}),
               QueryRuntimeException);

  EXPECT_THROW(EvaluateFunction("RIGHT", {132, 10}), QueryRuntimeException);
}

TEST_F(FunctionTest, Trimming) {
  EXPECT_TRUE(EvaluateFunction("LTRIM", {TypedValue::Null}).IsNull());
  EXPECT_TRUE(EvaluateFunction("RTRIM", {TypedValue::Null}).IsNull());
  EXPECT_TRUE(EvaluateFunction("TRIM", {TypedValue::Null}).IsNull());

  EXPECT_EQ(EvaluateFunction("LTRIM", {"  abc    "}).ValueString(), "abc    ");
  EXPECT_EQ(EvaluateFunction("RTRIM", {" abc "}).ValueString(), " abc");
  EXPECT_EQ(EvaluateFunction("TRIM", {"abc"}).ValueString(), "abc");

  EXPECT_THROW(EvaluateFunction("LTRIM", {"x", "y"}), QueryRuntimeException);
  EXPECT_THROW(EvaluateFunction("RTRIM", {"x", "y"}), QueryRuntimeException);
  EXPECT_THROW(EvaluateFunction("TRIM", {"x", "y"}), QueryRuntimeException);
}

TEST_F(FunctionTest, Reverse) {
  EXPECT_TRUE(EvaluateFunction("REVERSE", {TypedValue::Null}).IsNull());
  EXPECT_EQ(EvaluateFunction("REVERSE", {"abc"}).ValueString(), "cba");
  EXPECT_THROW(EvaluateFunction("REVERSE", {"x", "y"}), QueryRuntimeException);
}

TEST_F(FunctionTest, Replace) {
  EXPECT_THROW(EvaluateFunction("REPLACE", {}), QueryRuntimeException);
  EXPECT_TRUE(
      EvaluateFunction("REPLACE", {TypedValue::Null, "l", "w"}).IsNull());
  EXPECT_TRUE(
      EvaluateFunction("REPLACE", {"hello", TypedValue::Null, "w"}).IsNull());
  EXPECT_TRUE(
      EvaluateFunction("REPLACE", {"hello", "l", TypedValue::Null}).IsNull());
  EXPECT_EQ(EvaluateFunction("REPLACE", {"hello", "l", "w"}).ValueString(),
            "hewwo");

  EXPECT_THROW(EvaluateFunction("REPLACE", {1, "l", "w"}),
               QueryRuntimeException);
  EXPECT_THROW(EvaluateFunction("REPLACE", {"hello", 1, "w"}),
               QueryRuntimeException);
  EXPECT_THROW(EvaluateFunction("REPLACE", {"hello", "l", 1}),
               QueryRuntimeException);
}

TEST_F(FunctionTest, Split) {
  EXPECT_THROW(EvaluateFunction("SPLIT", {}), QueryRuntimeException);
  EXPECT_THROW(EvaluateFunction("SPLIT", {"one,two", 1}),
               QueryRuntimeException);
  EXPECT_THROW(EvaluateFunction("SPLIT", {1, "one,two"}),
               QueryRuntimeException);

  EXPECT_TRUE(
      EvaluateFunction("SPLIT", {TypedValue::Null, TypedValue::Null}).IsNull());
  EXPECT_TRUE(
      EvaluateFunction("SPLIT", {"one,two", TypedValue::Null}).IsNull());
  EXPECT_TRUE(EvaluateFunction("SPLIT", {TypedValue::Null, ","}).IsNull());

  auto result = EvaluateFunction("SPLIT", {"one,two", ","});
  EXPECT_TRUE(result.IsList());
  EXPECT_EQ(result.ValueList()[0].ValueString(), "one");
  EXPECT_EQ(result.ValueList()[1].ValueString(), "two");
}

TEST_F(FunctionTest, Substring) {
  EXPECT_THROW(EvaluateFunction("SUBSTRING", {}), QueryRuntimeException);

  EXPECT_TRUE(
      EvaluateFunction("SUBSTRING", {TypedValue::Null, 0, 10}).IsNull());
  EXPECT_THROW(
      EvaluateFunction("SUBSTRING", {TypedValue::Null, TypedValue::Null}),
      QueryRuntimeException);
  EXPECT_THROW(EvaluateFunction("SUBSTRING", {TypedValue::Null, -10}),
               QueryRuntimeException);
  EXPECT_THROW(
      EvaluateFunction("SUBSTRING", {TypedValue::Null, 0, TypedValue::Null}),
      QueryRuntimeException);
  EXPECT_THROW(EvaluateFunction("SUBSTRING", {TypedValue::Null, 0, -10}),
               QueryRuntimeException);

  EXPECT_EQ(EvaluateFunction("SUBSTRING", {"hello", 2}).ValueString(), "llo");
  EXPECT_EQ(EvaluateFunction("SUBSTRING", {"hello", 10}).ValueString(), "");
  EXPECT_EQ(EvaluateFunction("SUBSTRING", {"hello", 2, 0}).ValueString(), "");
  EXPECT_EQ(EvaluateFunction("SUBSTRING", {"hello", 1, 3}).ValueString(),
            "ell");
  EXPECT_EQ(EvaluateFunction("SUBSTRING", {"hello", 1, 4}).ValueString(),
            "ello");
  EXPECT_EQ(EvaluateFunction("SUBSTRING", {"hello", 1, 10}).ValueString(),
            "ello");
}

TEST_F(FunctionTest, ToLower) {
  EXPECT_THROW(EvaluateFunction("TOLOWER", {}), QueryRuntimeException);
  EXPECT_TRUE(EvaluateFunction("TOLOWER", {TypedValue::Null}).IsNull());
  EXPECT_EQ(EvaluateFunction("TOLOWER", {"Ab__C"}).ValueString(), "ab__c");
}

TEST_F(FunctionTest, ToUpper) {
  EXPECT_THROW(EvaluateFunction("TOUPPER", {}), QueryRuntimeException);
  EXPECT_TRUE(EvaluateFunction("TOUPPER", {TypedValue::Null}).IsNull());
  EXPECT_EQ(EvaluateFunction("TOUPPER", {"Ab__C"}).ValueString(), "AB__C");
}

}  // namespace
