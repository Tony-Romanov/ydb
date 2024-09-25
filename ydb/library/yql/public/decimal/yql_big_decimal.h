#pragma once

#include "yql_decimal.h"

namespace NYql {
namespace NDecimal {
namespace NBig {

template<ui8 Scale> struct TDivider;
#if defined(__clang__) && defined(DONT_USE_NATIVE_INT128)
template<> struct TDivider<0> { static inline constexpr TUint256 Value = 1U; };
template<ui8 Scale> struct TDivider { static inline constexpr TInt256 Value = TDivider<Scale - 1U>::Value * 10U; };
#else
template<> struct TDivider<0> { static constexpr TUint256 Value = 1U; };
template<ui8 Scale> struct TDivider { static constexpr TUint256 Value = TDivider<Scale - 1U>::Value * 10U; };
#endif

constexpr ui8 MaxPrecision = 76;

static_assert(sizeof(TInt256) == 32, "Wrong size of TInt256, expected 16");

inline constexpr TInt256 Inf() {
    return TInt256(10000000000000000000ULL) * TInt256(10000000000000000000ULL) * TInt256(10000000000000000000ULL) * TInt256(10000000000000000000ULL);
}

inline constexpr TInt256 Nan() {
    return Inf() + TInt256(1);
}

inline constexpr TInt256 Err() {
    return Nan() + TInt256(1);
}

TUint256 GetDivider(ui8 scale);

template<ui8 Precision>
inline constexpr TUint256 GetDivider() {
    return TDivider<Precision>::Value;
}

template<ui8 Precision, bool IncLow = false, bool DecHigh = false>
inline constexpr std::pair<TInt256, TInt256> GetBounds() {
    return std::make_pair(-GetDivider<Precision>() + (IncLow ? 1 : 0), +GetDivider<Precision>() - (DecHigh ? 1 : 0));
}

bool IsError(TInt256 v);
bool IsNan(TInt256 v);
bool IsInf(TInt256 v);

bool IsNormal(TInt256 v);
bool IsComparable(TInt256 v);

template<ui8 Precision>
inline bool IsNormal(TInt256 v) {
    const auto& b = GetBounds<Precision>();
    return v > b.first && v < b.second;
}

const char* ToString(TInt256 v, ui8 precision, ui8 scale = 0);
TInt256 FromString(const TStringBuf& str, ui8 precision, ui8 scale = 0);

// Accept string representation with exponent.
TInt256 FromStringEx(const TStringBuf& str, ui8 precision, ui8 scale);
/*
template<typename TMkqlProto>
inline TInt256 FromProto(const TMkqlProto& val) {
    ui64 half[2] = {val.GetLow128(), val.GetHi128()};
    TInt256 val128;
    std::memcpy(&val128, half, sizeof(val128));
    return val128;
}

template<typename TValue>
inline constexpr TValue YtDecimalNan() {
    return std::numeric_limits<TValue>::max();
}

template<>
inline constexpr TInt256 YtDecimalNan<TInt256>() {
    return ~(TInt256(1) << 127);
}

template<typename TValue>
inline constexpr TValue YtDecimalInf() {
    return YtDecimalNan<TValue>() - 1;
}

template<typename TValue>
inline TInt256 FromYtDecimal(TValue val) {
    static_assert(std::is_same<TInt256, TValue>::value || std::is_signed<TValue>::value, "Expected signed value");
    if (YtDecimalNan<TValue>() == val) {
        return Nan();
    } else if (YtDecimalInf<TValue>() == val) {
        return Inf();
    } else if (-YtDecimalInf<TValue>() == val) {
        return -Inf();
    } else {
        return TInt256(val);
    }
}

template<typename TValue>
inline TValue ToYtDecimal(TInt256 val) {
    static_assert(std::is_same<TInt256, TValue>::value || std::is_signed<TValue>::value, "Expected signed value");
    if (IsNormal(val)) {
        return (TValue)val;
    } else if (val == Inf()) {
        return YtDecimalInf<TValue>();
    } else if (val == -Inf()) {
        return -YtDecimalInf<TValue>();
    }
    return YtDecimalNan<TValue>();
}

inline TInt256 FromHalfs(ui64 lo, i64 hi) {
    ui64 half[2] = {lo, static_cast<ui64>(hi)};
    TInt256 val128;
    std::memcpy(&val128, half, sizeof(val128));
    return val128;
}

inline std::pair<ui64, ui64> MakePair(const TInt256 v) {
    std::pair<, ui64> r;
    std::memcpy(&r, &v, sizeof(v));
    return r;
    static_assert(sizeof(r) == sizeof(v), "Bad pair size.");
}
*/
bool IsValid(const TStringBuf& str);

// Round to nearest, ties to even.
TInt256 Div(TInt256 a, TInt256 b); // a/b
TInt256 Mul(TInt256 a, TInt256 b); // a*b
TInt256 Mod(TInt256 a, TInt256 b); // a%b

// a*b/c Only for non zero even normal positive divider.
TInt256 MulAndDivNormalDivider(TInt256 a, TInt256 b, TInt256 c);
// a*b/c Only for non zero normal positive multiplier.
TInt256 MulAndDivNormalMultiplier(TInt256 a, TInt256 b, TInt256 c);

struct TDecimal {
    TInt256 Value = 0;

    TDecimal() = default;

    template<typename T>
    TDecimal(T t): Value(t) { }

    explicit operator TInt256() const {
        return Value;
    }

    TDecimal& operator+=(TDecimal right) {
        const auto l = Value;
        const auto r = right.Value;
        const auto a = l + r;
        if (IsNormal(l) && IsNormal(r) && IsNormal(a)) {
            Value = a;
        } else if (IsNan(l) || IsNan(r) || !a /* inf - inf*/) {
            Value = Nan();
        } else {
            Value = a > 0
                ? +Inf()
                : -Inf();
        }
        return *this;
    }

    TDecimal& operator*=(TDecimal right) {
        Value = Mul(Value, right.Value);
        return *this;
    }

    TDecimal& operator/=(TDecimal right) {
        Value = Div(Value, right.Value);
        return *this;
    }

    friend TDecimal operator+(TDecimal left, TDecimal right) {
        left += right;
        return left;
    }

    friend TDecimal operator*(TDecimal left, TDecimal right) {
        left *= right;
        return left;
    }

    friend TDecimal operator/(TDecimal left, TDecimal right) {
        left /= right;
        return left;
    }
};

template<typename TRight>
class TDecimalMultiplicator {
protected:
    const TInt256 Bound;

public:
    TDecimalMultiplicator(
        ui8 precision,
        ui8 scale = 0 /* unused */)
        : Bound(GetDivider(precision))
    {
        Y_UNUSED(scale);
    }

    TInt256 Do(TInt256 left, TRight right) const {
        TInt256 mul = Mul(left, right);

        if (mul > -Bound && mul < +Bound)
            return mul;

        return IsNan(mul) ? Nan() : (mul > 0 ? +Inf() : -Inf());
    }
};

template<>
class TDecimalMultiplicator<TInt256> {
protected:
    const TInt256 Bound;
    const TInt256 Divider;

public:
    TDecimalMultiplicator(
        ui8 precision,
        ui8 scale)
        : Bound(GetDivider(precision))
        , Divider(GetDivider(scale))
    { }

    TInt256 Do(TInt256 left, TInt256 right) const {
        TInt256 mul = Divider > 1 ?
            MulAndDivNormalDivider(left, right, Divider):
            Mul(left, right);

        if (mul > -Bound && mul < +Bound)
            return mul;

        return IsNan(mul) ? Nan() : (mul > 0 ? +Inf() : -Inf());
    }
};

template<typename TRight>
class TDecimalDivisor {
public:
    TDecimalDivisor(
        ui8 precision = 0 /* unused */,
        ui8 scale = 0 /* unused */)
    {
        Y_UNUSED(precision);
        Y_UNUSED(scale);
    }

    TInt256 Do(TInt256 left, TRight right) const {
        return Div(left, right);
    }
};

template<>
class TDecimalDivisor<TInt256> {
protected:
    const TInt256 Bound;
    const TInt256 Divider;

public:
    TDecimalDivisor(
        ui8 precision,
        ui8 scale)
        : Bound(GetDivider(precision))
        , Divider(GetDivider(scale))
    { }

    TInt256 Do(TInt256 left, TInt256 right) const {
        TInt256 div = MulAndDivNormalMultiplier(left, Divider, right);
        if (div > -Bound && div < +Bound) {
            return div;
        }

        return IsNan(div) ? Nan() : (div > 0 ? +Inf() : -Inf());
    }
};

template<typename TRight>
class TDecimalRemainder {
protected:
    const TInt256 Bound;
    const TInt256 Divider;

public:
    TDecimalRemainder(
        ui8 precision,
        ui8 scale)
        : Bound(NYql::NDecimal::GetDivider(precision - scale))
        , Divider(NYql::NDecimal::GetDivider(scale))
    { }

    TInt256 Do(TInt256 left, TRight right) const {
        if constexpr (std::is_signed<TRight>::value) {
            if (TInt256(right) >= +Bound || TInt256(right) <= -Bound)
                return left;
        } else {
            if (TInt256(right) >= Bound)
                return left;
        }

        return Mod(left, Mul(Divider, right));
    }
};

template<>
class TDecimalRemainder<TInt256> {
public:
    TDecimalRemainder(
        ui8 precision = 0 /*unused*/,
        ui8 scale = 0 /*unused*/)
    {
        Y_UNUSED(precision);
        Y_UNUSED(scale);
    }

    TInt256 Do(TInt256 left, TInt256 right) const {
        return NYql::NDecimal::NBig::Mod(left, right);
    }
};

}
}
}
