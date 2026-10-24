// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdio>
#include <cstdlib>
#include "common/assert.h"
#include "video_core/renderer_metal/mtl_device_impl.h"

namespace Metal {

Device::Device() : impl{std::make_unique<Impl>()} {
    @autoreleasepool {
        impl->device = MTLCreateSystemDefaultDevice();
        ASSERT_MSG(impl->device, "No Metal device");
        impl->queue = [impl->device newCommandQueue];
        ASSERT_MSG(impl->queue, "Could not create the Metal command queue");

        // macOS 15: one residency set on the queue; the device-address buffers are added to it
        // when they appear and removed when they go (D6).
        MTLResidencySetDescriptor* descriptor = [MTLResidencySetDescriptor new];
        descriptor.label = @"bbport residency";
        NSError* error = nil;
        impl->residency = [impl->device newResidencySetWithDescriptor:descriptor error:&error];
        ASSERT_MSG(impl->residency, "Could not create the Metal residency set: {}",
                   error ? error.localizedDescription.UTF8String : "unknown");
        [impl->residency commit];
        [impl->queue addResidencySet:impl->residency];
        // Resident for good, not only while the queue has work (P6.1). The guest memory chunks
        // are not in the set (P8.5: in it they were unwired and rewired around idle GPU periods
        // all the same; the encoders declare them). BB_MTL_REQUEST_RESIDENCY=0 leaves it to the
        // queue (A/B).
        const char* request = std::getenv("BB_MTL_REQUEST_RESIDENCY");
        if (!(request && request[0] == '0')) {
            [impl->residency requestResidency];
        }
        const char* each = std::getenv("BB_MTL_REQUEST_EACH");
        impl->request_each_commit = each && each[0] == '1';

        name = impl->device.name.UTF8String;
    }
    std::printf("GPU: Metal device %s\n", name.c_str());
}

Device::~Device() = default;

} // namespace Metal
