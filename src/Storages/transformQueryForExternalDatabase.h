#pragma once

#include <base/types.h>
#include <Core/NamesAndTypes.h>
#include <Parsers/IdentifierQuotingStyle.h>
#include <Storages/SelectQueryInfo.h>
#include <Interpreters/Context_fwd.h>


namespace DB
{

class IAST;

/** For given ClickHouse query,
  * creates another query in a form of
  *
  * SELECT columns... FROM db.table WHERE ...
  *
  * where 'columns' are all required columns to read from "left" table of original query,
  * and WHERE contains subset of (AND-ed) conditions from original query,
  * that contain only compatible expressions.
  *
  * If limit is passed additionally apply LIMIT in result query.
  *
  * Compatible expressions are comparisons of identifiers, constants, and logical operations on them.
  *
  * Throws INCORRECT_QUERY if external_table_strict_query (from context settings)
  * is set and some expression from WHERE is not compatible.
  */
String transformQueryForExternalDatabase(
    const SelectQueryInfo & query_info,
    const Names & column_names,
    const NamesAndTypesList & available_columns,
    IdentifierQuotingStyle identifier_quoting_style,
    LiteralEscapingStyle literal_escaping_style,
    const String & database,
    const String & table,
    ContextPtr context,
    std::optional<size_t> limit = {});

}
