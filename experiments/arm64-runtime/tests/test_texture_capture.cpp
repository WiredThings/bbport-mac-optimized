// SPDX-License-Identifier: GPL-2.0-or-later
// Authored metadata only: actual FrameCapture implementation, no GPU/device/window/game.
#include "video_core/frame_capture.h"
// Original GPU executable ABI providers; renamed entry is never called.
#define main unused_original_gpu_main
#include "../vendor/metal_api/bb_gpu/bb_gpu_main.cpp"
#undef main
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <string>
int main(int argc,char** argv) {
 assert(argc==2);const std::string mode=argv[1];
 const bool expected=mode=="on"||mode=="overflow"||mode=="late-private";
 if(VideoCore::FrameCapture::TextureBindingsEnabled()!=expected)return 10;
 if(VideoCore::FrameCapture::Active())return 11;
 if(mode=="no-trigger"||mode=="unsafe-late") {
  VideoCore::FrameCapture::Native("{\"op\":\"must_not_record\"}");
  if(VideoCore::FrameCapture::Active())return 12;
 }else{
  const std::string late=mode=="late-private"?std::string(std::getenv("BB_TEXTURE_BINDING_CAPTURE_DIR"))+"/trigger":"";
  const char* trigger=late.empty()?std::getenv("BB_CAPTURE_TRIGGER"):late.c_str();assert(trigger);
  {std::ofstream out(trigger);out<<"fixture\n";}
  VideoCore::FrameCapture::OnFlip(0x1234000);
  VideoCore::FrameCapture::FrameBoundary(1);
  if(!VideoCore::FrameCapture::NativeRecording())return 13;
  VideoCore::FrameCapture::Native("{\"op\":\"texture_authored_fixture\",\"guest_data\":false}");
  VideoCore::FrameCapture::Native("{\"op\":\"memory_live\",\"fixture\":true}");
  VideoCore::FrameCapture::Native("{\"op\":\"texture_lifetime\",\"fixture\":true}");
  VideoCore::FrameCapture::Native("{\"op\":\"texture_binding\",\"fixture\":true}");
  VideoCore::FrameCapture::Native("{\"op\":\"texture_authored_separator\"}");
  VideoCore::FrameCapture::Native("{\"op\":\"texture_binding\",\"fixture\":true}");
  VideoCore::FrameCapture::Native("{\"op\":\"texture_authored_separator\"}");
  if(mode=="overflow")for(int n=0;n<9000;++n)VideoCore::FrameCapture::Native("{\"op\":\"texture_authored_overflow\"}");
  VideoCore::FrameCapture::FrameBoundary(2);
  if(VideoCore::FrameCapture::NativeRecording())return 14;
 }
 puts("AUTHORED_TEXTURE_CAPTURE_PASS");
}
