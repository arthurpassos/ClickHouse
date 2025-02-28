#pragma once

#include <Interpreters/Context_fwd.h>
#include <Interpreters/InDepthNodeVisitor.h>

namespace DB
{

class ASTSubquery;
class ASTFunction;
struct ASTTableExpression;
class Block;

/** Replace subqueries that return exactly one row
    * ("scalar" subqueries) to the corresponding constants.
    *
    * If the subquery returns more than one column, it is replaced by a tuple of constants.
    *
    * Features
    *
    * A replacement occurs during query analysis, and not during the main runtime, so
    * the query result can be used for the index in the table.
    *
    * Scalar subqueries are executed on the request-initializer server.
    * The request is sent to remote servers with already substituted constants.
    */
class ExecuteScalarSubqueriesMatcher
{
public:
    using Visitor = InDepthNodeVisitor<ExecuteScalarSubqueriesMatcher, true>;

    struct Data : public WithContext
    {
        size_t subquery_depth;
        Scalars & scalars;
        Scalars & local_scalars;
        bool only_analyze;
        bool is_create_parameterized_view;
        bool replace_only_to_literals;
        std::optional<size_t> max_literal_size;
    };

    static bool needChildVisit(ASTPtr & node, const ASTPtr &);
    static void visit(ASTPtr & ast, Data & data);

private:
    static void visit(const ASTSubquery & subquery, ASTPtr & ast, Data & data);
    static void visit(const ASTFunction & func, ASTPtr & ast, Data & data);
};

using ExecuteScalarSubqueriesVisitor = ExecuteScalarSubqueriesMatcher::Visitor;

bool worthConvertingScalarToLiteral(const Block & scalar, std::optional<size_t> max_literal_size);

}
