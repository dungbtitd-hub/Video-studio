// Splits per-pixel work on large frames across cores (export at 4K). Small
// frames (preview, thumbnails) run inline: starting threads would cost more.
#pragma once

#include <algorithm>
#include <cstddef>
#include <thread>
#include <vector>

namespace vstudio::detail {

// Calls fn(y0, y1) for disjoint row ranges covering [0, rows). Blocks until done.
template <class Fn>
void parallelRows(int rows, std::size_t pixels, Fn&& fn) {
    constexpr std::size_t kMinPixelsPerThread = 96 * 1024;
    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    const int n = static_cast<int>(std::min<std::size_t>({hw, pixels / kMinPixelsPerThread, static_cast<std::size_t>(rows)}));
    if (n <= 1) {
        fn(0, rows);
        return;
    }
    std::vector<std::thread> threads;
    threads.reserve(n - 1);
    const int step = (rows + n - 1) / n;
    for (int i = 1; i < n; ++i) {
        const int y0 = i * step, y1 = std::min(rows, y0 + step);
        if (y0 < y1) threads.emplace_back([&fn, y0, y1] { fn(y0, y1); });
    }
    fn(0, std::min(rows, step));
    for (auto& t : threads) t.join();
}

}  // namespace vstudio::detail
