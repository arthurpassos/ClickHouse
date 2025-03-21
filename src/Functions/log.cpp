#include <Functions/FunctionMathUnary.h>
#include <Functions/FunctionFactory.h>

namespace DB
{

namespace ErrorCodes
{
extern const int NOT_IMPLEMENTED;
}

namespace
{

struct LogName { static constexpr auto name = "log"; };

#if USE_FASTOPS

    struct Impl
    {
        static constexpr auto name = LogName::name;
        static constexpr auto rows_per_iteration = 0;
        static constexpr bool always_returns_float64 = false;

        template <typename T>
        static void execute(const T * src, size_t size, T * dst)
        {
            if constexpr (std::is_same_v<T, BFloat16>)
            {
                throw Exception(ErrorCodes::NOT_IMPLEMENTED, "Function `{}` is not implemented for BFloat16", name);
            }
            else
            {
                NFastOps::Log<true>(src, size, dst);
            }
        }
    };

using FunctionLog = FunctionMathUnary<Impl>;

#else
using FunctionLog = FunctionMathUnary<UnaryFunctionVectorized<LogName, log>>;
#endif

}

REGISTER_FUNCTION(Log)
{
    factory.registerFunction<FunctionLog>({}, FunctionFactory::Case::Insensitive);
    factory.registerAlias("ln", "log", FunctionFactory::Case::Insensitive);
}

}
