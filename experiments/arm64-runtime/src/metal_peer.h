// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <filesystem>
#include <memory>
class MetalPeer {
public:
 MetalPeer(const std::filesystem::path& executable,const std::filesystem::path& state);
 ~MetalPeer();
 MetalPeer(const MetalPeer&)=delete;
 MetalPeer& operator=(const MetalPeer&)=delete;
 bool Idle();
private:
 struct Impl;
 std::unique_ptr<Impl> impl;
};
