// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <string>

namespace Metal {

/// The Metal device, the one command queue and the residency set attached to it (D6, D7). The
/// Objective-C objects live in Impl (mtl_device_impl.h, included by .mm files only).
class Device {
public:
    Device();
    ~Device();

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    struct Impl;
    Impl& GetImpl() const {
        return *impl;
    }

    const std::string& Name() const {
        return name;
    }

private:
    std::unique_ptr<Impl> impl;
    std::string name;
};

} // namespace Metal
