// SPDX-License-Identifier: MIT
#pragma once
#include "bb_guest_frame_policy.h"
#include "bb_stutter_audit.h"
namespace BbGuestFrame {
inline State& ThreadState(){static thread_local State s;return s;}
inline bool Requested(){static const bool requested=[] {const auto*v=std::getenv("BB_GUEST_FRAME_AUDIT");return v&&v[0]=='1';}();return requested;}
struct Clock {U Mono(){return BbAudit::Now();}U Cpu(){return BbAudit::Now(CLOCK_THREAD_CPUTIME_ID);}};
struct Sink {
 void Span(U start,U wall,U cpu,U op){BbAudit::Record(BbAudit::GuestSubmit,start,wall,cpu,op);}
 void FrameWindow(U start,U wall,U cpu,U op){BbAudit::Record(BbAudit::GuestFrameCpuWindow,start,wall,cpu,op);}
 void Marker(U end,U op){BbAudit::Record(BbAudit::GuestFlipMarker,end,0,0,op);}
 void Status(U end,U packed,U accounting){BbAudit::Record(BbAudit::GuestFrameStatus,end,0,0,packed);BbAudit::Record(BbAudit::GuestFrameAccounting,end,0,0,accounting);}
};
}
