///
/// @file
/// This file provides macros for easier construction of openCypher query AST.
/// The usage of macros is very similar to how one would write openCypher. For
/// example:
///
///     AstTreeStorage storage;  // Macros rely on storage being in scope.
///     // PROPERTY_LOOKUP and PROPERTY_PAIR macros rely on database::SingleNode
///     database::SingleNode db;
///
///     QUERY(MATCH(PATTERN(NODE("n"), EDGE("e"), NODE("m"))),
///           WHERE(LESS(PROPERTY_LOOKUP("e", edge_prop), LITERAL(3))),
///           RETURN(SUM(PROPERTY_LOOKUP("m", prop)), AS("sum"),
///                  ORDER_BY(IDENT("sum")),
///                  SKIP(ADD(LITERAL(1), LITERAL(2)))));
///
/// Each of the macros is accompanied by a function. The functions use overload
/// resolution and template magic to provide a type safe way of constructing
/// queries. Although the functions can be used by themselves, it is more
/// convenient to use the macros.
///

#pragma once

#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "database/graph_db.hpp"
#include "database/graph_db_accessor.hpp"
#include "query/frontend/ast/ast.hpp"
#include "query/interpret/awesome_memgraph_functions.hpp"
#include "storage/types.hpp"
#include "utils/string.hpp"

namespace query {

namespace test_common {

template <typename T>
auto ToList(const TypedValue &t) {
  std::vector<T> list;
  for (auto x : t.Value<std::vector<TypedValue>>()) {
    list.push_back(x.Value<T>());
  }
  return list;
};

template <typename TElement>
auto ToMap(const TypedValue &t) {
  std::map<std::string, TElement> map;
  for (const auto &kv : t.Value<std::map<std::string, TypedValue>>())
    map.emplace(kv.first, kv.second.Value<TElement>());
  return map;
};

// Custom types for ORDER BY, SKIP, LIMIT, ON MATCH and ON CREATE expressions,
// so that they can be used to resolve function calls.
struct OrderBy {
  std::vector<std::pair<Ordering, Expression *>> expressions;
};
struct Skip {
  Expression *expression = nullptr;
};
struct Limit {
  Expression *expression = nullptr;
};
struct OnMatch {
  std::vector<Clause *> set;
};
struct OnCreate {
  std::vector<Clause *> set;
};

// Helper functions for filling the OrderBy with expressions.
auto FillOrderBy(OrderBy &order_by, Expression *expression,
                 Ordering ordering = Ordering::ASC) {
  order_by.expressions.emplace_back(ordering, expression);
}
template <class... T>
auto FillOrderBy(OrderBy &order_by, Expression *expression, Ordering ordering,
                 T... rest) {
  FillOrderBy(order_by, expression, ordering);
  FillOrderBy(order_by, rest...);
}
template <class... T>
auto FillOrderBy(OrderBy &order_by, Expression *expression, T... rest) {
  FillOrderBy(order_by, expression);
  FillOrderBy(order_by, rest...);
}

///
/// Create OrderBy expressions.
///
/// The supported combination of arguments is: (Expression, [Ordering])+
/// Since the Ordering is optional, by default it is ascending.
///
template <class... T>
auto GetOrderBy(T... exprs) {
  OrderBy order_by;
  FillOrderBy(order_by, exprs...);
  return order_by;
}

///
/// Create PropertyLookup with given name and property.
///
/// Name is used to create the Identifier which is used for property lookup.
///
auto GetPropertyLookup(AstTreeStorage &storage, database::GraphDb &db,
                       const std::string &name, storage::Property property) {
  database::GraphDbAccessor dba(db);
  return storage.Create<PropertyLookup>(storage.Create<Identifier>(name),
                                        dba.PropertyName(property), property);
}
auto GetPropertyLookup(AstTreeStorage &storage, database::GraphDb &db,
                       Expression *expr, storage::Property property) {
  database::GraphDbAccessor dba(db);
  return storage.Create<PropertyLookup>(expr, dba.PropertyName(property),
                                        property);
}
auto GetPropertyLookup(
    AstTreeStorage &storage, database::GraphDb &, const std::string &name,
    const std::pair<std::string, storage::Property> &prop_pair) {
  return storage.Create<PropertyLookup>(storage.Create<Identifier>(name),
                                        prop_pair.first, prop_pair.second);
}
auto GetPropertyLookup(
    AstTreeStorage &storage, database::GraphDb &, Expression *expr,
    const std::pair<std::string, storage::Property> &prop_pair) {
  return storage.Create<PropertyLookup>(expr, prop_pair.first,
                                        prop_pair.second);
}

///
/// Create an EdgeAtom with given name, direction and edge_type.
///
/// Name is used to create the Identifier which is assigned to the edge.
///
auto GetEdge(AstTreeStorage &storage, const std::string &name,
             EdgeAtom::Direction dir = EdgeAtom::Direction::BOTH,
             const std::vector<storage::EdgeType> &edge_types = {}) {
  return storage.Create<EdgeAtom>(storage.Create<Identifier>(name),
                                  EdgeAtom::Type::SINGLE, dir, edge_types);
}

///
/// Create a variable length expansion EdgeAtom with given name, direction and
/// edge_type.
///
/// Name is used to create the Identifier which is assigned to the edge.
///
auto GetEdgeVariable(AstTreeStorage &storage, const std::string &name,
                     EdgeAtom::Direction dir = EdgeAtom::Direction::BOTH,
                     const std::vector<storage::EdgeType> &edge_types = {},
                     Identifier *inner_edge = nullptr,
                     Identifier *inner_node = nullptr) {
  auto r_val =
      storage.Create<EdgeAtom>(storage.Create<Identifier>(name),
                               EdgeAtom::Type::DEPTH_FIRST, dir, edge_types);
  r_val->filter_lambda_.inner_edge =
      inner_edge ? inner_edge
                 : storage.Create<Identifier>(utils::RandomString(20));
  r_val->filter_lambda_.inner_node =
      inner_node ? inner_node
                 : storage.Create<Identifier>(utils::RandomString(20));
  return r_val;
}

///
/// Create a NodeAtom with given name and label.
///
/// Name is used to create the Identifier which is assigned to the node.
///
auto GetNode(AstTreeStorage &storage, const std::string &name,
             std::experimental::optional<storage::Label> label =
                 std::experimental::nullopt) {
  auto node = storage.Create<NodeAtom>(storage.Create<Identifier>(name));
  if (label) node->labels_.emplace_back(*label);
  return node;
}

///
/// Create a Pattern with given atoms.
///
auto GetPattern(AstTreeStorage &storage, std::vector<PatternAtom *> atoms) {
  auto pattern = storage.Create<Pattern>();
  pattern->identifier_ =
      storage.Create<Identifier>(utils::RandomString(20), false);
  pattern->atoms_.insert(pattern->atoms_.begin(), atoms.begin(), atoms.end());
  return pattern;
}

///
/// Create a Pattern with given name and atoms.
///
auto GetPattern(AstTreeStorage &storage, const std::string &name,
                std::vector<PatternAtom *> atoms) {
  auto pattern = storage.Create<Pattern>();
  pattern->identifier_ = storage.Create<Identifier>(name, true);
  pattern->atoms_.insert(pattern->atoms_.begin(), atoms.begin(), atoms.end());
  return pattern;
}

///
/// This function fills an AST node which with given patterns.
///
/// The function is most commonly used to create Match and Create clauses.
///
template <class TWithPatterns>
auto GetWithPatterns(TWithPatterns *with_patterns,
                     std::vector<Pattern *> patterns) {
  with_patterns->patterns_.insert(with_patterns->patterns_.begin(),
                                  patterns.begin(), patterns.end());
  return with_patterns;
}

///
/// Create a query with given clauses.
///

auto GetSingleQuery(SingleQuery *single_query, Clause *clause) {
  single_query->clauses_.emplace_back(clause);
  return single_query;
}
auto GetSingleQuery(SingleQuery *single_query, Match *match, Where *where) {
  match->where_ = where;
  single_query->clauses_.emplace_back(match);
  return single_query;
}
auto GetSingleQuery(SingleQuery *single_query, With *with, Where *where) {
  with->where_ = where;
  single_query->clauses_.emplace_back(with);
  return single_query;
}
template <class... T>
auto GetSingleQuery(SingleQuery *single_query, Match *match, Where *where,
                    T *... clauses) {
  match->where_ = where;
  single_query->clauses_.emplace_back(match);
  return GetSingleQuery(single_query, clauses...);
}
template <class... T>
auto GetSingleQuery(SingleQuery *single_query, With *with, Where *where,
                    T *... clauses) {
  with->where_ = where;
  single_query->clauses_.emplace_back(with);
  return GetSingleQuery(single_query, clauses...);
}

template <class... T>
auto GetSingleQuery(SingleQuery *single_query, Clause *clause, T *... clauses) {
  single_query->clauses_.emplace_back(clause);
  return GetSingleQuery(single_query, clauses...);
}

auto GetCypherUnion(CypherUnion *cypher_union, SingleQuery *single_query) {
  cypher_union->single_query_ = single_query;
  return cypher_union;
}

auto GetQuery(AstTreeStorage &storage, SingleQuery *single_query) {
  storage.query()->single_query_ = single_query;
  return storage.query();
}

auto GetQuery(AstTreeStorage &storage, SingleQuery *single_query,
              CypherUnion *cypher_union) {
  storage.query()->cypher_unions_.emplace_back(cypher_union);
  return GetQuery(storage, single_query);
}

template <class... T>
auto GetQuery(AstTreeStorage &storage, SingleQuery *single_query,
              CypherUnion *cypher_union, T *... cypher_unions) {
  storage.query()->cypher_unions_.emplace_back(cypher_union);
  return GetQuery(storage, single_query, cypher_unions...);
}

// Helper functions for constructing RETURN and WITH clauses.
void FillReturnBody(AstTreeStorage &, ReturnBody &body,
                    NamedExpression *named_expr) {
  body.named_expressions.emplace_back(named_expr);
}
void FillReturnBody(AstTreeStorage &storage, ReturnBody &body,
                    const std::string &name) {
  auto *ident = storage.Create<query::Identifier>(name);
  auto *named_expr = storage.Create<query::NamedExpression>(name, ident);
  body.named_expressions.emplace_back(named_expr);
}
void FillReturnBody(AstTreeStorage &, ReturnBody &body, Limit limit) {
  body.limit = limit.expression;
}
void FillReturnBody(AstTreeStorage &, ReturnBody &body, Skip skip,
                    Limit limit = Limit{}) {
  body.skip = skip.expression;
  body.limit = limit.expression;
}
void FillReturnBody(AstTreeStorage &, ReturnBody &body, OrderBy order_by,
                    Limit limit = Limit{}) {
  body.order_by = order_by.expressions;
  body.limit = limit.expression;
}
void FillReturnBody(AstTreeStorage &, ReturnBody &body, OrderBy order_by,
                    Skip skip, Limit limit = Limit{}) {
  body.order_by = order_by.expressions;
  body.skip = skip.expression;
  body.limit = limit.expression;
}
void FillReturnBody(AstTreeStorage &, ReturnBody &body, Expression *expr,
                    NamedExpression *named_expr) {
  // This overload supports `RETURN(expr, AS(name))` construct, since
  // NamedExpression does not inherit Expression.
  named_expr->expression_ = expr;
  body.named_expressions.emplace_back(named_expr);
}
void FillReturnBody(AstTreeStorage &storage, ReturnBody &body,
                    const std::string &name, NamedExpression *named_expr) {
  named_expr->expression_ = storage.Create<query::Identifier>(name);
  body.named_expressions.emplace_back(named_expr);
}
template <class... T>
void FillReturnBody(AstTreeStorage &storage, ReturnBody &body, Expression *expr,
                    NamedExpression *named_expr, T... rest) {
  named_expr->expression_ = expr;
  body.named_expressions.emplace_back(named_expr);
  FillReturnBody(storage, body, rest...);
}
template <class... T>
void FillReturnBody(AstTreeStorage &storage, ReturnBody &body,
                    NamedExpression *named_expr, T... rest) {
  body.named_expressions.emplace_back(named_expr);
  FillReturnBody(storage, body, rest...);
}
template <class... T>
void FillReturnBody(AstTreeStorage &storage, ReturnBody &body,
                    const std::string &name, NamedExpression *named_expr,
                    T... rest) {
  named_expr->expression_ = storage.Create<query::Identifier>(name);
  body.named_expressions.emplace_back(named_expr);
  FillReturnBody(storage, body, rest...);
}
template <class... T>
void FillReturnBody(AstTreeStorage &storage, ReturnBody &body,
                    const std::string &name, T... rest) {
  auto *ident = storage.Create<query::Identifier>(name);
  auto *named_expr = storage.Create<query::NamedExpression>(name, ident);
  body.named_expressions.emplace_back(named_expr);
  FillReturnBody(storage, body, rest...);
}

///
/// Create the return clause with given expressions.
///
/// The supported expression combination of arguments is:
///
/// (String | NamedExpression | (Expression NamedExpression))+
/// [OrderBy] [Skip] [Limit]
///
/// When the pair (Expression NamedExpression) is given, the Expression will be
/// moved inside the NamedExpression. This is done, so that the constructs like
/// RETURN(expr, AS("name"), ...) are supported. Taking a String is a shorthand
/// for RETURN(IDENT(string), AS(string), ....).
///
/// @sa GetWith
template <class... T>
auto GetReturn(AstTreeStorage &storage, bool distinct, T... exprs) {
  auto ret = storage.Create<Return>();
  ret->body_.distinct = distinct;
  FillReturnBody(storage, ret->body_, exprs...);
  return ret;
}

///
/// Create the with clause with given expressions.
///
/// The supported expression combination is the same as for @c GetReturn.
///
/// @sa GetReturn
template <class... T>
auto GetWith(AstTreeStorage &storage, bool distinct, T... exprs) {
  auto with = storage.Create<With>();
  with->body_.distinct = distinct;
  FillReturnBody(storage, with->body_, exprs...);
  return with;
}

///
/// Create the UNWIND clause with given named expression.
///
auto GetUnwind(AstTreeStorage &storage, NamedExpression *named_expr) {
  return storage.Create<query::Unwind>(named_expr);
}
auto GetUnwind(AstTreeStorage &storage, Expression *expr, NamedExpression *as) {
  as->expression_ = expr;
  return GetUnwind(storage, as);
}

///
/// Create the delete clause with given named expressions.
///
auto GetDelete(AstTreeStorage &storage, std::vector<Expression *> exprs,
               bool detach = false) {
  auto del = storage.Create<Delete>();
  del->expressions_.insert(del->expressions_.begin(), exprs.begin(),
                           exprs.end());
  del->detach_ = detach;
  return del;
}

///
/// Create a set property clause for given property lookup and the right hand
/// side expression.
///
auto GetSet(AstTreeStorage &storage, PropertyLookup *prop_lookup,
            Expression *expr) {
  return storage.Create<SetProperty>(prop_lookup, expr);
}

///
/// Create a set properties clause for given identifier name and the right hand
/// side expression.
///
auto GetSet(AstTreeStorage &storage, const std::string &name, Expression *expr,
            bool update = false) {
  return storage.Create<SetProperties>(storage.Create<Identifier>(name), expr,
                                       update);
}

///
/// Create a set labels clause for given identifier name and labels.
///
auto GetSet(AstTreeStorage &storage, const std::string &name,
            std::vector<storage::Label> labels) {
  return storage.Create<SetLabels>(storage.Create<Identifier>(name), labels);
}

///
/// Create a remove property clause for given property lookup
///
auto GetRemove(AstTreeStorage &storage, PropertyLookup *prop_lookup) {
  return storage.Create<RemoveProperty>(prop_lookup);
}

///
/// Create a remove labels clause for given identifier name and labels.
///
auto GetRemove(AstTreeStorage &storage, const std::string &name,
               std::vector<storage::Label> labels) {
  return storage.Create<RemoveLabels>(storage.Create<Identifier>(name), labels);
}

///
/// Create a Merge clause for given Pattern with optional OnMatch and OnCreate
/// parts.
///
auto GetMerge(AstTreeStorage &storage, Pattern *pattern,
              OnCreate on_create = OnCreate{}) {
  auto *merge = storage.Create<query::Merge>();
  merge->pattern_ = pattern;
  merge->on_create_ = on_create.set;
  return merge;
}
auto GetMerge(AstTreeStorage &storage, Pattern *pattern, OnMatch on_match,
              OnCreate on_create = OnCreate{}) {
  auto *merge = storage.Create<query::Merge>();
  merge->pattern_ = pattern;
  merge->on_match_ = on_match.set;
  merge->on_create_ = on_create.set;
  return merge;
}

}  // namespace test_common

}  // namespace query

///
/// All the following macros implicitly pass `storage` variable to functions.
/// You need to have `AstTreeStorage storage;` somewhere in scope to use them.
/// Refer to function documentation to see what the macro does.
///
/// Example usage:
///
///   // Create MATCH (n) -[r]- (m) RETURN m AS new_name
///   AstTreeStorage storage;
///   auto query = QUERY(MATCH(PATTERN(NODE("n"), EDGE("r"), NODE("m"))),
///                      RETURN(NEXPR("new_name"), IDENT("m")));
///
#define NODE(...) query::test_common::GetNode(storage, __VA_ARGS__)
#define EDGE(...) query::test_common::GetEdge(storage, __VA_ARGS__)
#define EDGE_VARIABLE(...) \
  query::test_common::GetEdgeVariable(storage, __VA_ARGS__)
#define PATTERN(...) query::test_common::GetPattern(storage, {__VA_ARGS__})
#define NAMED_PATTERN(name, ...) \
  query::test_common::GetPattern(storage, name, {__VA_ARGS__})
#define OPTIONAL_MATCH(...)                                               \
  query::test_common::GetWithPatterns(storage.Create<query::Match>(true), \
                                      {__VA_ARGS__})
#define MATCH(...)                                                    \
  query::test_common::GetWithPatterns(storage.Create<query::Match>(), \
                                      {__VA_ARGS__})
#define WHERE(expr) storage.Create<query::Where>((expr))
#define CREATE(...)                                                    \
  query::test_common::GetWithPatterns(storage.Create<query::Create>(), \
                                      {__VA_ARGS__})
#define IDENT(name) storage.Create<query::Identifier>((name))
#define LITERAL(val) storage.Create<query::PrimitiveLiteral>((val))
#define LIST(...)                     \
  storage.Create<query::ListLiteral>( \
      std::vector<query::Expression *>{__VA_ARGS__})
#define MAP(...)                                                    \
  storage.Create<query::MapLiteral>(                                \
      std::unordered_map<std::pair<std::string, storage::Property>, \
                         query::Expression *>{__VA_ARGS__})
#define PROPERTY_PAIR(property_name) \
  std::make_pair(property_name,      \
                 database::GraphDbAccessor(db).Property(property_name))
#define PROPERTY_LOOKUP(...) \
  query::test_common::GetPropertyLookup(storage, db, __VA_ARGS__)
#define NEXPR(name, expr) storage.Create<query::NamedExpression>((name), (expr))
// AS is alternative to NEXPR which does not initialize NamedExpression with
// Expression. It should be used with RETURN or WITH. For example:
// RETURN(IDENT("n"), AS("n")) vs. RETURN(NEXPR("n", IDENT("n"))).
#define AS(name) storage.Create<query::NamedExpression>((name))
#define RETURN(...) query::test_common::GetReturn(storage, false, __VA_ARGS__)
#define WITH(...) query::test_common::GetWith(storage, false, __VA_ARGS__)
#define RETURN_DISTINCT(...) \
  query::test_common::GetReturn(storage, true, __VA_ARGS__)
#define WITH_DISTINCT(...) \
  query::test_common::GetWith(storage, true, __VA_ARGS__)
#define UNWIND(...) query::test_common::GetUnwind(storage, __VA_ARGS__)
#define ORDER_BY(...) query::test_common::GetOrderBy(__VA_ARGS__)
#define SKIP(expr) \
  query::test_common::Skip { (expr) }
#define LIMIT(expr) \
  query::test_common::Limit { (expr) }
#define DELETE(...) query::test_common::GetDelete(storage, {__VA_ARGS__})
#define DETACH_DELETE(...) \
  query::test_common::GetDelete(storage, {__VA_ARGS__}, true)
#define SET(...) query::test_common::GetSet(storage, __VA_ARGS__)
#define REMOVE(...) query::test_common::GetRemove(storage, __VA_ARGS__)
#define MERGE(...) query::test_common::GetMerge(storage, __VA_ARGS__)
#define ON_MATCH(...)                            \
  query::test_common::OnMatch {                  \
    std::vector<query::Clause *> { __VA_ARGS__ } \
  }
#define ON_CREATE(...)                           \
  query::test_common::OnCreate {                 \
    std::vector<query::Clause *> { __VA_ARGS__ } \
  }
#define CREATE_INDEX_ON(label, property) \
  storage.Create<query::CreateIndex>((label), (property))
#define QUERY(...) query::test_common::GetQuery(storage, __VA_ARGS__)
#define SINGLE_QUERY(...) \
  query::test_common::GetSingleQuery(storage.Create<SingleQuery>(), __VA_ARGS__)
#define UNION(...)                                                      \
  query::test_common::GetCypherUnion(storage.Create<CypherUnion>(true), \
                                     __VA_ARGS__)
#define UNION_ALL(...)                                                   \
  query::test_common::GetCypherUnion(storage.Create<CypherUnion>(false), \
                                     __VA_ARGS__)
// Various operators
#define ADD(expr1, expr2) \
  storage.Create<query::AdditionOperator>((expr1), (expr2))
#define LESS(expr1, expr2) storage.Create<query::LessOperator>((expr1), (expr2))
#define LESS_EQ(expr1, expr2) \
  storage.Create<query::LessEqualOperator>((expr1), (expr2))
#define GREATER(expr1, expr2) \
  storage.Create<query::GreaterOperator>((expr1), (expr2))
#define GREATER_EQ(expr1, expr2) \
  storage.Create<query::GreaterEqualOperator>((expr1), (expr2))
#define SUM(expr)                                     \
  storage.Create<query::Aggregation>((expr), nullptr, \
                                     query::Aggregation::Op::SUM)
#define COUNT(expr)                                   \
  storage.Create<query::Aggregation>((expr), nullptr, \
                                     query::Aggregation::Op::COUNT)
#define AVG(expr)                                     \
  storage.Create<query::Aggregation>((expr), nullptr, \
                                     query::Aggregation::Op::AVG)
#define COLLECT_LIST(expr)                            \
  storage.Create<query::Aggregation>((expr), nullptr, \
                                     query::Aggregation::Op::COLLECT_LIST)
#define EQ(expr1, expr2) storage.Create<query::EqualOperator>((expr1), (expr2))
#define NEQ(expr1, expr2) \
  storage.Create<query::NotEqualOperator>((expr1), (expr2))
#define AND(expr1, expr2) storage.Create<query::AndOperator>((expr1), (expr2))
#define OR(expr1, expr2) storage.Create<query::OrOperator>((expr1), (expr2))
// Function call
#define FN(function_name, ...)           \
  storage.Create<query::Function>(       \
      utils::ToUpperCase(function_name), \
      std::vector<query::Expression *>{__VA_ARGS__})
// List slicing
#define SLICE(list, lower_bound, upper_bound) \
  storage.Create<query::ListSlicingOperator>(list, lower_bound, upper_bound)
// all(variable IN list WHERE predicate)
#define ALL(variable, list, where)                                        \
  storage.Create<query::All>(storage.Create<query::Identifier>(variable), \
                             list, where)
#define REDUCE(accumulator, initializer, variable, list, expr)     \
  storage.Create<query::Reduce>(                                   \
      storage.Create<query::Identifier>(accumulator), initializer, \
      storage.Create<query::Identifier>(variable), list, expr)
