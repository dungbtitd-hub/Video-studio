// UTF-8 <-> std::filesystem::path helpers (C++20 made u8path/u8string char8_t-based).
#pragma once

#include <filesystem>
#include <string>

namespace vstudio::detail {

inline std::filesystem::path toPath(const std::string& utf8) {
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

inline std::string fromPath(const std::filesystem::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

inline std::string fromPathGeneric(const std::filesystem::path& p) {
    const std::u8string s = p.generic_u8string();
    return std::string(s.begin(), s.end());
}

}  // namespace vstudio::detail
