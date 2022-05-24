#include "FunctionsStringSearch.h"
#include "FunctionFactory.h"
#include "MatchImpl.h"

namespace DB
{
namespace
{

struct NameNotILike
{
    static constexpr auto name = "notILike";
};

using NotILikeImpl = MatchImpl<NameNotILike, MatchSyntax::Like, NegateMatch::Yes, MatchCaseInsensitively::Yes>;
using FunctionNotILike = FunctionsStringSearch<NotILikeImpl>;

}

void registerFunctionNotILike(FunctionFactory & factory)
{
    factory.registerFunction<FunctionNotILike>();
}
}
