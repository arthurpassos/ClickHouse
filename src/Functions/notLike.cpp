#include "FunctionsStringSearch.h"
#include "FunctionFactory.h"
#include "MatchImpl.h"

namespace DB
{
namespace
{

struct NameNotLike
{
    static constexpr auto name = "notLike";
};

using FunctionNotLike = FunctionsStringSearch<MatchImpl<NameNotLike, MatchSyntax::Like, NegateMatch::Yes, MatchCaseInsensitively::No>>;

}

void registerFunctionNotLike(FunctionFactory & factory)
{
    factory.registerFunction<FunctionNotLike>();
}

}
