#include <Storages/PartitionStrategy.h>
#include <Parsers/ASTFunction.h>
#include <Parsers/ASTLiteral.h>
#include <Interpreters/TreeRewriter.h>
#include <Interpreters/ExpressionAnalyzer.h>
#include <Storages/PartitionedSink.h>
#include <Interpreters/Context.h>
#include <Storages/KeyDescription.h>
#include <Core/Settings.h>
#include <Storages/ColumnsDescription.h>

namespace DB
{

namespace ErrorCodes
{
extern const int LOGICAL_ERROR;
extern const int BAD_ARGUMENTS;
}

namespace
{
    HiveStylePartitionStrategy::PartitionExpressionActionsAndColumnName buildExpressionHive(
        ASTPtr partition_by,
        const NamesAndTypesList & partition_columns,
        const Block & sample_block,
        ContextPtr context)
    {
        HiveStylePartitionStrategy::PartitionExpressionActionsAndColumnName actions_with_column_name;
        ASTs concat_args;

        if (const auto * tuple_function = partition_by->as<ASTFunction>();
            tuple_function && tuple_function->name == "tuple")
        {
            chassert(tuple_function->arguments->children.size() == partition_columns.size());

            std::size_t index = 0;

            for (const auto & partition_column : partition_columns)
            {
                const auto & child = tuple_function->arguments->children[index++];

                concat_args.push_back(std::make_shared<ASTLiteral>(partition_column.name + "="));

                concat_args.push_back(makeASTFunction("toString", child));

                concat_args.push_back(std::make_shared<ASTLiteral>("/"));
            }
        }
        else
        {
            chassert(partition_columns.size() == 1);

            ASTs to_string_args = {1, partition_by};
            concat_args.push_back(std::make_shared<ASTLiteral>(partition_columns.front().name + "="));
            concat_args.push_back(makeASTFunction("toString", std::move(to_string_args)));
            concat_args.push_back(std::make_shared<ASTLiteral>("/"));
        }

        ASTPtr hive_expr = makeASTFunction("concat", std::move(concat_args));
        auto hive_syntax_result = TreeRewriter(context).analyze(hive_expr, sample_block.getNamesAndTypesList());
        actions_with_column_name.actions = ExpressionAnalyzer(hive_expr, hive_syntax_result, context).getActions(false);
        actions_with_column_name.column_name = hive_expr->getColumnName();

        return actions_with_column_name;
    }

    Block buildBlockWithoutPartitionColumns(
        const Block & sample_block,
        const std::unordered_set<std::string> & partition_expression_required_columns_set)
    {
        Block result;
        for (size_t i = 0; i < sample_block.columns(); i++)
        {
            if (!partition_expression_required_columns_set.contains(sample_block.getByPosition(i).name))
            {
                result.insert(sample_block.getByPosition(i));
            }
        }

        return result;
    }
}

std::unordered_map<std::string, bool> PartitionStrategy::partition_strategy_to_wildcard_acceptance =
{
    {"wildcard", true},
    {"hive", false}
};

PartitionStrategy::PartitionStrategy(ASTPtr partition_by_, const Block & sample_block_, ContextPtr context_)
: partition_by(partition_by_), sample_block(sample_block_), context(context_)
{
    auto key_description = KeyDescription::getKeyFromAST(partition_by, ColumnsDescription::fromNamesAndTypes(sample_block.getNamesAndTypes()), context);
    partition_columns = key_description.sample_block.getNamesAndTypesList();
}

const NamesAndTypesList & PartitionStrategy::getPartitionColumns() const
{
    return partition_columns;
}

struct HivePartitionStrategyFactory
{
    static std::shared_ptr<PartitionStrategy> get(
        ASTPtr partition_by,
        const Block & sample_block,
        ContextPtr context,
        bool globbed_path,
        bool partition_columns_in_data_file)
    {
        if (!partition_by)
        {
            throw Exception(ErrorCodes::BAD_ARGUMENTS, "Partition strategy hive can not be used without a PARTITION BY expression");
        }

        if (globbed_path)
        {
            throw Exception(ErrorCodes::BAD_ARGUMENTS, "Partition strategy {} can not be used with a globbed path", "hive");
        }

        return std::make_shared<HiveStylePartitionStrategy>(
            partition_by,
            sample_block,
            context,
            partition_columns_in_data_file);
    }
};

struct WildcardPartitionStrategyFactory
{
    static std::shared_ptr<PartitionStrategy> get(
        ASTPtr partition_by,
        const Block & sample_block,
        ContextPtr context,
        bool partition_columns_in_data_file)
    {
        if (!partition_by)
        {
            return nullptr;
        }

        if (!partition_columns_in_data_file)
        {
            throw Exception(ErrorCodes::BAD_ARGUMENTS, "Partition strategy {} can not be used with partition_columns_in_data_file=0", "wildcard");
        }

        // in theory, we should not accept wildcard partition strategy without a wildcard in the path
        // but it has been made that way by default, it just won't include the partition id in the filepath

        return std::make_shared<StringifiedPartitionStrategy>(partition_by, sample_block, context);
    }
};

std::shared_ptr<PartitionStrategy> PartitionStrategyFactory::get(ASTPtr partition_by,
                                                                 const Block & sample_block,
                                                                 ContextPtr context,
                                                                 bool globbed_path,
                                                                 const std::string & partition_strategy,
                                                                 bool partition_columns_in_data_file)
{
    if (partition_strategy == "hive")
    {
        return HivePartitionStrategyFactory::get(
            partition_by,
            sample_block,
            context,
            globbed_path,
            partition_columns_in_data_file);
    }

    if (partition_strategy == "wildcard")
    {
        return WildcardPartitionStrategyFactory::get(partition_by, sample_block, context, partition_columns_in_data_file);
    }

    throw Exception(ErrorCodes::BAD_ARGUMENTS,
                "Unknown partitioning style '{}'",
                partition_strategy);
}

std::shared_ptr<PartitionStrategy> PartitionStrategyFactory::get(ASTPtr partition_by,
                                                                 const NamesAndTypesList & partition_columns,
                                                                 ContextPtr context,
                                                                 bool globbed_path,
                                                                 const std::string & partition_strategy,
                                                                 bool partition_columns_in_data_file)
{
    Block block;
    for (const auto & partition_column : partition_columns)
    {
        block.insert({partition_column.type, partition_column.name});
    }

    return get(partition_by, block, context, globbed_path, partition_strategy, partition_columns_in_data_file);
}

StringifiedPartitionStrategy::StringifiedPartitionStrategy(ASTPtr partition_by_, const Block & sample_block_, ContextPtr context_)
    : PartitionStrategy(partition_by_, sample_block_, context_)
{
    ASTs arguments(1, partition_by);
    ASTPtr partition_by_string = makeASTFunction("toString", std::move(arguments));
    auto syntax_result = TreeRewriter(context).analyze(partition_by_string, sample_block.getNamesAndTypesList());
    actions_with_column_name.actions = ExpressionAnalyzer(partition_by_string, syntax_result, context).getActions(false);
    actions_with_column_name.column_name = partition_by_string->getColumnName();
}

HiveStylePartitionStrategy::HiveStylePartitionStrategy(
    ASTPtr partition_by_,
    const Block & sample_block_,
    ContextPtr context_,
    bool partition_columns_in_data_file_)
    : PartitionStrategy(partition_by_, sample_block_, context_),
    partition_columns_in_data_file(partition_columns_in_data_file_)
{
    for (const auto & partition_column : partition_columns)
    {
        partition_columns_name_set.insert(partition_column.name);
    }
    actions_with_column_name = buildExpressionHive(partition_by, partition_columns, sample_block, context);
    block_without_partition_columns = buildBlockWithoutPartitionColumns(sample_block, partition_columns_name_set);
}

Chunk HiveStylePartitionStrategy::getFormatChunk(const Chunk & chunk)
{
    Chunk result;

    if (partition_columns_in_data_file)
    {
        for (const auto & column : chunk.getColumns())
        {
            result.addColumn(column);
        }

        return result;
    }

    chassert(chunk.getColumns().size() == sample_block.columns());

    for (size_t i = 0; i < sample_block.columns(); i++)
    {
        if (!partition_columns_name_set.contains(sample_block.getByPosition(i).name))
        {
            result.addColumn(chunk.getColumns()[i]);
        }
    }

    return result;
}

Block HiveStylePartitionStrategy::getFormatHeader()
{
    if (partition_columns_in_data_file)
    {
        return sample_block;
    }

    return block_without_partition_columns;
}

}
