// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string_view>

inline bool BbThdHttpDestination(std::string_view scheme, std::string_view host, unsigned port) {
    constexpr std::string_view expected = "thehuntersdream.com";
    if (scheme != "https" || host.size() != expected.size() ||
        (port != 443 && port != 18671 && port != 20443)) return false;
    for (size_t i = 0; i < host.size(); ++i) {
        const char c = host[i] >= 'A' && host[i] <= 'Z' ? host[i] + ('a' - 'A') : host[i];
        if (c != expected[i]) return false;
    }
    return true;
}
