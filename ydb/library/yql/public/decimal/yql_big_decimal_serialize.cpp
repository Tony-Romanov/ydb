#include "yql_big_decimal_serialize.h"

#include <utility>

namespace NYql {
namespace NDecimal {
namespace NBig {

size_t Serialize(TInt256 value, char* buf) {
    if (value == -Nan()) {
        *buf = 0x00;
        return 1U;
    }

    if (value == -Inf()) {
        *buf = 0x01;
        return 1U;
    }

    if (value == +Inf()) {
        *buf = 0xFE;
        return 1U;
    }

    if (value == +Nan()) {
        *buf = 0xFF;
        return 1U;
    }

    auto size = sizeof(value);
    auto p = reinterpret_cast<const char*>(&value) + size;

    if (*(p - 1U) & 0x80) {
        while (size && ~0 == *--p)
            --size;
        *buf = 0x7F - size;
    } else {
        while (size && 0 == *--p)
            --size;
        *buf = 0x80 + size;
    }

    for (auto i = 0U; i < size; ++i) {
        *++buf = *p--;
    }

    return ++size;
}

std::pair<TInt256, size_t> Deserialize(const char* b, size_t len) {
    if (!b || len == 0U)
        return std::make_pair(Err(), 0U);

    const auto mark = ui8(*b);
    const bool neg = mark < 0x80u;
    if (mark == 0x00u || mark == 0xFFu)
        return std::make_pair(neg ? -Nan() : +Nan(), 1U);
    if (mark == 0x01u || mark == 0xFEu)
        return std::make_pair(neg ? -Inf() : +Inf(), 1U);

    if (mark < 0x5Fu || mark > 0xA0u) {
        return std::make_pair(Err(), 0U);
    }

    auto used = neg ? 0x7Fu - mark : mark - 0x80u;
    if (len <= used)
        return std::make_pair(Err(), 0U);

    TInt256 v;
    const auto size = sizeof(v);
    auto p = reinterpret_cast<char*>(&v) + size;

    for (auto fill = size - used + 1U; --fill;)
        *--p = neg ? ~0 : 0;

    for (auto copy = used + 1U; --copy;)
        *--p = *++b;

    return std::make_pair(v, ++used);
}

}
}
}
