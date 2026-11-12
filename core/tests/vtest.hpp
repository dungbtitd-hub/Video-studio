// Minimal self-contained test harness (no external dependency, builds on every
// target including Android/iOS cross builds).
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace vtest {

template <class T>
std::string show(const T& v) {
    if constexpr (requires(std::ostream& o) { o << v; }) {
        std::ostringstream os;
        os << v;
        return os.str();
    } else if constexpr (requires { v.ticks(); }) {
        return std::to_string(v.ticks()) + " ticks";
    } else {
        return "<value>";
    }
}

struct Case {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}

struct Failure {
    std::string msg;
};

struct Registrar {
    Registrar(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); }
};

inline int runAll(int argc, char** argv) {
    const std::string filter = argc > 1 ? argv[1] : "";
    int failed = 0, ran = 0;
    for (auto& c : registry()) {
        if (!filter.empty() && std::string(c.name).find(filter) == std::string::npos) continue;
        ++ran;
        try {
            c.fn();
            std::printf("[ OK ] %s\n", c.name);
        } catch (const Failure& f) {
            ++failed;
            std::printf("[FAIL] %s\n       %s\n", c.name, f.msg.c_str());
        } catch (const std::exception& e) {
            ++failed;
            std::printf("[FAIL] %s\n       unexpected exception: %s\n", c.name, e.what());
        }
    }
    std::printf("\n%d/%d passed\n", ran - failed, ran);
    return failed ? 1 : 0;
}

}  // namespace vtest

#define VT_CAT2(a, b) a##b
#define VT_CAT(a, b) VT_CAT2(a, b)
#define TEST(name)                                                                   \
    static void VT_CAT(test_fn_, __LINE__)();                                        \
    static vtest::Registrar VT_CAT(test_reg_, __LINE__)(name, VT_CAT(test_fn_, __LINE__)); \
    static void VT_CAT(test_fn_, __LINE__)()

#define CHECK(cond)                                                                                 \
    do {                                                                                            \
        if (!(cond)) {                                                                              \
            std::ostringstream os_;                                                                 \
            os_ << __FILE__ << ":" << __LINE__ << ": CHECK(" #cond ") failed";                    \
            throw vtest::Failure{os_.str()};                                                        \
        }                                                                                           \
    } while (0)

#define CHECK_EQ(a, b)                                                                              \
    do {                                                                                            \
        const auto va_ = (a);                                                                       \
        const auto vb_ = (b);                                                                       \
        if (!(va_ == vb_)) {                                                                        \
            std::ostringstream os_;                                                                 \
            os_ << __FILE__ << ":" << __LINE__ << ": CHECK_EQ(" #a ", " #b ") failed: "           \
                << vtest::show(va_) << " vs " << vtest::show(vb_);                                                                \
            throw vtest::Failure{os_.str()};                                                        \
        }                                                                                           \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                       \
    do {                                                                                            \
        const double va_ = (a), vb_ = (b);                                                          \
        if (std::fabs(va_ - vb_) > (eps)) {                                                         \
            std::ostringstream os_;                                                                 \
            os_ << __FILE__ << ":" << __LINE__ << ": CHECK_NEAR failed: " << va_ << " vs " << vb_; \
            throw vtest::Failure{os_.str()};                                                        \
        }                                                                                           \
    } while (0)

#define CHECK_THROWS(expr)                                                                          \
    do {                                                                                            \
        bool threw_ = false;                                                                        \
        try {                                                                                       \
            (void)(expr);                                                                           \
        } catch (...) {                                                                             \
            threw_ = true;                                                                          \
        }                                                                                           \
        if (!threw_) throw vtest::Failure{std::string(__FILE__) + ": expected exception: " #expr}; \
    } while (0)
