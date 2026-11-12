// Video Studio Core - time representation.
//
// All timeline/source times are integer ticks ("flicks", 1/705'600'000 s).
// A flick divides evenly into every common video frame duration
// (23.976, 24, 25, 29.97, 30, 48, 50, 59.94, 60, 90, 100, 120 fps) and every
// common audio sample period (8k, 16k, 22.05k, 32k, 44.1k, 48k, 88.2k, 96k Hz),
// so frame/sample positions convert without accumulated floating-point error.
// int64 range covers roughly +/-414 years.
#pragma once

#include <cstdint>
#include <compare>
#include <string>

namespace vstudio {

struct Rational {
    std::int64_t num = 0;
    std::int64_t den = 1;

    constexpr Rational() = default;
    constexpr Rational(std::int64_t n, std::int64_t d = 1) : num(n), den(d) {}

    [[nodiscard]] Rational reduced() const;
    [[nodiscard]] bool valid() const { return den != 0; }
    [[nodiscard]] double toDouble() const { return den ? static_cast<double>(num) / static_cast<double>(den) : 0.0; }
    [[nodiscard]] Rational inverse() const { return {den, num}; }
    [[nodiscard]] std::string toString() const;  // "30000/1001"
    static bool parse(const std::string& s, Rational& out);

    friend bool operator==(const Rational& a, const Rational& b);
    friend std::strong_ordering operator<=>(const Rational& a, const Rational& b);
};

enum class Rounding { Down, Up, Nearest };

// Computes value * num / den with 128-bit intermediate and the given rounding.
std::int64_t mulDiv(std::int64_t value, std::int64_t num, std::int64_t den, Rounding r = Rounding::Nearest);

class Time {
public:
    static constexpr std::int64_t kTicksPerSecond = 705'600'000;

    constexpr Time() = default;
    static constexpr Time fromTicks(std::int64_t t) { return Time(t); }
    static constexpr Time zero() { return Time(0); }
    static Time fromSeconds(double s);
    static Time fromMilliseconds(std::int64_t ms) { return Time(ms * (kTicksPerSecond / 1000)); }
    // value expressed in units of `timebase` seconds (e.g. FFmpeg pts with time_base 1/90000).
    static Time fromTimebase(std::int64_t value, Rational timebase, Rounding r = Rounding::Nearest);
    // Start time of frame `index` at frame rate `fps` (frames per second).
    static Time fromFrame(std::int64_t index, Rational fps);
    // Start time of sample `index` at `sampleRate` Hz.
    static Time fromSample(std::int64_t index, int sampleRate);

    [[nodiscard]] constexpr std::int64_t ticks() const { return ticks_; }
    [[nodiscard]] double seconds() const { return static_cast<double>(ticks_) / kTicksPerSecond; }
    [[nodiscard]] std::int64_t toTimebase(Rational timebase, Rounding r = Rounding::Nearest) const;
    // Index of the frame that is showing at this time (floor).
    [[nodiscard]] std::int64_t toFrame(Rational fps, Rounding r = Rounding::Down) const;
    [[nodiscard]] std::int64_t toSample(int sampleRate, Rounding r = Rounding::Down) const;
    // Snap to the start of the frame containing this time.
    [[nodiscard]] Time snapToFrame(Rational fps) const { return fromFrame(toFrame(fps), fps); }
    // Scales a duration by a rational factor (used for clip speed).
    [[nodiscard]] Time scaled(Rational factor, Rounding r = Rounding::Nearest) const;
    // "HH:MM:SS:FF" timecode at the given fps (non-drop-frame).
    [[nodiscard]] std::string timecode(Rational fps) const;

    constexpr Time operator+(Time o) const { return Time(ticks_ + o.ticks_); }
    constexpr Time operator-(Time o) const { return Time(ticks_ - o.ticks_); }
    constexpr Time operator-() const { return Time(-ticks_); }
    constexpr Time& operator+=(Time o) { ticks_ += o.ticks_; return *this; }
    constexpr Time& operator-=(Time o) { ticks_ -= o.ticks_; return *this; }
    constexpr auto operator<=>(const Time&) const = default;

private:
    constexpr explicit Time(std::int64_t t) : ticks_(t) {}
    std::int64_t ticks_ = 0;
};

// Half-open interval [start, end).
struct TimeRange {
    Time start;
    Time end;
    [[nodiscard]] Time duration() const { return end - start; }
    [[nodiscard]] bool contains(Time t) const { return t >= start && t < end; }
    [[nodiscard]] bool overlaps(const TimeRange& o) const { return start < o.end && o.start < end; }
    [[nodiscard]] bool empty() const { return end <= start; }
};

}  // namespace vstudio
