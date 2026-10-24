// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string_view>
#include <cstdlib>
#include <charconv>

inline bool BbThdHostEqual(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
        if (lower(a[i]) != lower(b[i])) return false;
    }
    return true;
}
inline bool BbThdOriginMatches(std::string_view value, std::string_view host, unsigned port) {
    constexpr std::string_view prefix = "https://";
    if (value.substr(0, prefix.size()) != prefix) return false;
    value.remove_prefix(prefix.size());
    if (!value.empty() && value.back() == '/') value.remove_suffix(1);
    if (value.find_first_of("/@?#") != std::string_view::npos) return false;
    unsigned expected = 443;
    const auto colon = value.rfind(':');
    if (colon != std::string_view::npos && (value.empty() || value.back() != ']')) {
        const auto text = value.substr(colon + 1);
        auto result = std::from_chars(text.data(), text.data() + text.size(), expected);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || !expected || expected > 65535) return false;
        value = value.substr(0, colon);
    }
    if (value.size() > 2 && value.front() == '[' && value.back() == ']') { value.remove_prefix(1); value.remove_suffix(1); }
    return expected == port && BbThdHostEqual(value, host);
}
inline bool BbThdHttpDestination(std::string_view scheme, std::string_view host, unsigned port) {
    if (scheme != "https") return false;
    bool configured = false;
    for (const char* key : {"BB_THD_HTTP_ORIGIN", "BB_THD_AUTH", "BB_THD_SERVER"}) {
        if (const char* value = std::getenv(key); value && *value) {
            configured = true;
            if (BbThdOriginMatches(value, host, port)) return true;
        }
    }
    return !configured && (port == 443 || port == 18671 || port == 20443) && BbThdHostEqual(host, "thehuntersdream.com");
}
