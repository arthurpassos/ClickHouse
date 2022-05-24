#pragma once

#include "MatchImpl.h"
#include "FunctionsStringSearch.h"

namespace DB
{

struct NameLike
{
    static constexpr auto name = "like";
};

using LikeImpl = MatchImpl<NameLike, MatchSyntax::Like, NegateMatch::No, MatchCaseInsensitively::No>;
using FunctionLike = FunctionsStringSearch<LikeImpl>;

}
