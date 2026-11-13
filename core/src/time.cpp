#include "vstudio/time.hpp"

#include <cmath>
#include <cstdio>
#include <numeric>
#include <stdexcept>

namespace vstudio {

// 128-bit integers exist on 64-bit GCC/Clang targets, not on MSVC or 32-bit ARM/x86.
#if defined(__SIZEOF_INT128__)
#define VSTUDIO_HAS_I128 1
__extension__ typedef __int128 i128;
#else
#define VSTUDIO_HAS_I128 0
#endif

namespace {
#if !VSTUDIO_HAS_I128
// No i128: split the division so the products stay in 64 bits, with exact correction.
std::int64_t mulDivImpl(std::int64_t v, std::int64_t n, std::int64_t d, Rounding r) {
    // Split v = q*d + rem to keep products small: v*n/d = q*n + rem*n/d
    const std::int64_t q = v / d;
    const std::int64_t rem = v % d;
    // rem*n may overflow for huge n; acceptable for timebases used in practice (< 2^31).
    const std::int64_t p = rem * n;
    std::int64_t base = q * n;
    std::int64_t fq = p / d;
    std::int64_t fr = p % d;
    if (fr != 0) {
        const bool neg = (fr < 0) != (d < 0);
        switch (r) {
            case Rounding::Down: if (neg) --fq; break;
            case Rounding::Up: if (!neg) ++fq; break;
            case Rounding::Nearest: {
                const std::int64_t twice = 2 * (fr < 0 ? -fr : fr);
                const std::int64_t ad = d < 0 ? -d : d;
                if (twice >= ad) fq += neg ? -1 : 1;
                break;
            }
        }
    }
    return base + fq;
}
#else
std::int64_t mulDivImpl(std::int64_t v, std::int64_t n, std::int64_t d, Rounding r) {
    i128 p = static_cast<i128>(v) * n;
    i128 q = p / d;
    i128 rem = p % d;
    if (rem != 0) {
        const bool neg = (rem < 0) != (d < 0);
        switch (r) {
            case Rounding::Down: if (neg) --q; break;
            case Rounding::Up: if (!neg) ++q; break;
            case Rounding::Nearest: {
                i128 twice = 2 * (rem < 0 ? -rem : rem);
                i128 ad = d < 0 ? -d : d;
                if (twice >= ad) q += neg ? -1 : 1;
                break;
            }
        }
    }
    return static_cast<std::int64_t>(q);
}
#endif
}  // namespace

std::int64_t mulDiv(std::int64_t value, std::int64_t num, std::int64_t den, Rounding r) {
    if (den == 0) throw std::invalid_argument("mulDiv: zero denominator");
    return mulDivImpl(value, num, den, r);
}

Rational Rational::reduced() const {
    if (den == 0) return *this;
    std::int64_t g = std::gcd(num, den);
    if (g == 0) g = 1;
    Rational out{num / g, den / g};
    if (out.den < 0) { out.num = -out.num; out.den = -out.den; }
    return out;
}

std::string Rational::toString() const { return std::to_string(num) + "/" + std::to_string(den); }

bool Rational::parse(const std::string& s, Rational& out) {
    try {
        const auto slash = s.find('/');
        if (slash == std::string::npos) {
            out = Rational(std::stoll(s), 1);
        } else {
            out = Rational(std::stoll(s.substr(0, slash)), std::stoll(s.substr(slash + 1)));
        }
        return out.den != 0;
    } catch (...) {
        return false;
    }
}

bool operator==(const Rational& a, const Rational& b) {
    const Rational x = a.reduced(), y = b.reduced();
    return x.num == y.num && x.den == y.den;
}

std::strong_ordering operator<=>(const Rational& a, const Rational& b) {
    const Rational x = a.reduced(), y = b.reduced();
#if !VSTUDIO_HAS_I128
    const long double l = static_cast<long double>(x.num) * y.den;
    const long double rr = static_cast<long double>(y.num) * x.den;
    if (l < rr) return std::strong_ordering::less;
    if (l > rr) return std::strong_ordering::greater;
    return std::strong_ordering::equal;
#else
    const i128 l = static_cast<i128>(x.num) * y.den;
    const i128 rr = static_cast<i128>(y.num) * x.den;
    if (l < rr) return std::strong_ordering::less;
    if (l > rr) return std::strong_ordering::greater;
    return std::strong_ordering::equal;
#endif
}

Time Time::fromSeconds(double s) { return Time(static_cast<std::int64_t>(std::llround(s * kTicksPerSecond))); }

Time Time::fromTimebase(std::int64_t value, Rational tb, Rounding r) {
    // seconds = value * tb.num / tb.den ; ticks = seconds * kTicksPerSecond
    return Time(mulDiv(value, tb.num * kTicksPerSecond, tb.den, r));
}

std::int64_t Time::toTimebase(Rational tb, Rounding r) const {
    return mulDiv(ticks_, tb.den, tb.num * kTicksPerSecond, r);
}

Time Time::fromFrame(std::int64_t index, Rational fps) {
    return Time(mulDiv(index, fps.den * kTicksPerSecond, fps.num, Rounding::Nearest));
}

std::int64_t Time::toFrame(Rational fps, Rounding r) const {
    return mulDiv(ticks_, fps.num, fps.den * kTicksPerSecond, r);
}

Time Time::fromSample(std::int64_t index, int sampleRate) {
    return Time(mulDiv(index, kTicksPerSecond, sampleRate, Rounding::Nearest));
}

std::int64_t Time::toSample(int sampleRate, Rounding r) const {
    return mulDiv(ticks_, sampleRate, kTicksPerSecond, r);
}

Time Time::scaled(Rational f, Rounding r) const { return Time(mulDiv(ticks_, f.num, f.den, r)); }

std::string Time::timecode(Rational fps) const {
    const bool neg = ticks_ < 0;
    const std::int64_t frame = (neg ? -*this : *this).toFrame(fps);
    // Nominal integer frame rate for the frames field (29.97 -> 30).
    const std::int64_t nominal = std::max<std::int64_t>(1, mulDiv(1, fps.num, fps.den, Rounding::Nearest));
    const std::int64_t ff = frame % nominal;
    const std::int64_t totalSec = frame / nominal;
    char buf[48];
    std::snprintf(buf, sizeof buf, "%s%02lld:%02lld:%02lld:%02lld", neg ? "-" : "",
                  static_cast<long long>(totalSec / 3600), static_cast<long long>((totalSec / 60) % 60),
                  static_cast<long long>(totalSec % 60), static_cast<long long>(ff));
    return buf;
}

}  // namespace vstudio
