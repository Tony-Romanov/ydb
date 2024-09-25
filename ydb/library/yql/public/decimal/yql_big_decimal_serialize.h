#pragma once

#include <ydb/library/yql/public/decimal/yql_big_decimal.h>

namespace NYql {
namespace NDecimal {
namespace NBig {

// big-endian 16 bytes buffer.
size_t Serialize(TInt256 v, char* buff);
std::pair<TInt256, size_t> Deserialize(const char* buff, size_t len);

}
}
}
