#include "core/guest_cpu/hle_call_adapter.h"
#include "/Users/Shared/BloodEchoesDevelopment/arm64-runtime-prototype/vendor/metal_api/shadps4/core/libraries/gnmdriver/gnmdriver.h"
#include "/Users/Shared/BloodEchoesDevelopment/arm64-runtime-prototype/vendor/metal_api/shadps4/core/libraries/videoout/video_out.h"
#include "/Users/Shared/BloodEchoesDevelopment/arm64-runtime-prototype/vendor/metal_api/shadps4/core/libraries/kernel/equeue.h"
#include "/Users/Shared/BloodEchoesDevelopment/arm64-runtime-prototype/vendor/metal_api/shadps4/core/libraries/kernel/time.h"
namespace Libraries::Kernel {
s32 PS4_SYSV_ABI posix_kqueue();
s32 PS4_SYSV_ABI posix_kevent(s32 handle, OrbisKernelEvent* changelist, u64 nchanges,
                              OrbisKernelEvent* eventlist, u64 nevents,
                              OrbisKernelTimespec* timeout);
int PS4_SYSV_ABI sceKernelCreateEqueue(OrbisKernelEqueue* eq, const char* name);
int PS4_SYSV_ABI sceKernelDeleteEqueue(OrbisKernelEqueue eq);
int PS4_SYSV_ABI sceKernelWaitEqueue(OrbisKernelEqueue eq, OrbisKernelEvent* ev, int num, int* out,
                                     OrbisKernelUseconds* timo);
s32 PS4_SYSV_ABI sceKernelAddHRTimerEvent(OrbisKernelEqueue eq, int id, OrbisKernelTimespec* ts,
                                          void* udata);
int PS4_SYSV_ABI sceKernelDeleteHRTimerEvent(OrbisKernelEqueue eq, int id);
int PS4_SYSV_ABI sceKernelAddTimerEvent(OrbisKernelEqueue eq, int id, OrbisKernelUseconds usec,
                                        void* udata);
int PS4_SYSV_ABI sceKernelDeleteTimerEvent(OrbisKernelEqueue eq, int id);
int PS4_SYSV_ABI sceKernelAddUserEvent(OrbisKernelEqueue eq, int id);
int PS4_SYSV_ABI sceKernelAddUserEventEdge(OrbisKernelEqueue eq, int id);
void* PS4_SYSV_ABI sceKernelGetEventUserData(const OrbisKernelEvent* ev);
u64 PS4_SYSV_ABI sceKernelGetEventId(const OrbisKernelEvent* ev);
int PS4_SYSV_ABI sceKernelTriggerUserEvent(OrbisKernelEqueue eq, int id, void* udata);
int PS4_SYSV_ABI sceKernelDeleteUserEvent(OrbisKernelEqueue eq, int id);
int PS4_SYSV_ABI sceKernelGetEventFilter(const OrbisKernelEvent* ev);
u64 PS4_SYSV_ABI sceKernelGetEventData(const OrbisKernelEvent* ev);
}
using namespace Core::GuestCpu;
std::shared_ptr<HleCallAdapter> bb_arm_gpu_adapter(std::string_view name,uintptr_t address) {
if(name=="sceGnmDrawInitDefaultHardwareState200") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmDrawInitDefaultHardwareState200)>(address));
if(name=="sceGnmSetPsShader") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmSetPsShader)>(address));
if(name=="sceGnmUpdatePsShader") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmUpdatePsShader)>(address));
if(name=="sceGnmSetVsShader") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmSetVsShader)>(address));
if(name=="sceGnmUpdateVsShader") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmUpdateVsShader)>(address));
if(name=="sceGnmSetEsShader") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmSetEsShader)>(address));
if(name=="sceGnmSetHsShader") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmSetHsShader)>(address));
if(name=="sceGnmUpdateHsShader") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmUpdateHsShader)>(address));
if(name=="sceGnmSetLsShader") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmSetLsShader)>(address));
if(name=="sceGnmSetCsShader") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmSetCsShader)>(address));
if(name=="sceGnmSetGsShader") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmSetGsShader)>(address));
if(name=="sceGnmUpdateGsShader") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmUpdateGsShader)>(address));
if(name=="sceGnmInsertSetMarker") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmInsertSetMarker)>(address));
if(name=="sceGnmInsertSetColorMarker") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmInsertSetColorMarker)>(address));
if(name=="sceGnmInsertPushMarker") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmInsertPushMarker)>(address));
if(name=="sceGnmInsertPushColorMarker") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmInsertPushColorMarker)>(address));
if(name=="sceGnmInsertPopMarker") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmInsertPopMarker)>(address));
if(name=="sceGnmInsertWaitFlipDone") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmInsertWaitFlipDone)>(address));
if(name=="sceGnmLogicalCuIndexToPhysicalCuIndex") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmLogicalCuIndexToPhysicalCuIndex)>(address));
if(name=="sceGnmLogicalCuMaskToPhysicalCuMask") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmLogicalCuMaskToPhysicalCuMask)>(address));
if(name=="sceGnmSubmitCommandBuffers") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmSubmitCommandBuffers)>(address));
if(name=="sceGnmSubmitAndFlipCommandBuffers") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmSubmitAndFlipCommandBuffers)>(address));
if(name=="sceGnmValidateCommandBuffers") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmValidateCommandBuffers)>(address));
if(name=="sceGnmAddEqEvent") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmAddEqEvent)>(address));
if(name=="sceGnmDeleteEqEvent") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmDeleteEqEvent)>(address));
if(name=="sceGnmGetEqEventType") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmGetEqEventType)>(address));
if(name=="sceGnmGetEqTimeStamp") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmGetEqTimeStamp)>(address));
if(name=="sceGnmMapComputeQueue") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmMapComputeQueue)>(address));
if(name=="sceGnmMapComputeQueueWithPriority") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmMapComputeQueueWithPriority)>(address));
if(name=="sceGnmUnmapComputeQueue") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmUnmapComputeQueue)>(address));
if(name=="sceGnmDingDong") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmDingDong)>(address));
if(name=="sceGnmFlushGarlic") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmFlushGarlic)>(address));
if(name=="sceGnmDebugHardwareStatus") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmDebugHardwareStatus)>(address));
if(name=="sceGnmSubmitDone") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmSubmitDone)>(address));
if(name=="sceGnmAreSubmitsAllowed") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmAreSubmitsAllowed)>(address));
if(name=="sceGnmRequestFlipAndSubmitDone") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmRequestFlipAndSubmitDone)>(address));
if(name=="sceGnmSetupMipStatsReport") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmSetupMipStatsReport)>(address));
if(name=="sceGnmRequestMipStatsReportAndReset") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmRequestMipStatsReportAndReset)>(address));
if(name=="sceGnmDisableMipStatsReport") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmDisableMipStatsReport)>(address));
if(name=="sceGnmSetGsRingSizes") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmSetGsRingSizes)>(address));
if(name=="sceGnmGetTheTessellationFactorRingBufferBaseAddress") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmGetTheTessellationFactorRingBufferBaseAddress)>(address));
if(name=="sceRazorIsLoaded") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceRazorIsLoaded)>(address));
if(name=="sceRazorCaptureImmediate") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceRazorCaptureImmediate)>(address));
if(name=="sceRazorCaptureSinceLastFlip") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceRazorCaptureSinceLastFlip)>(address));
if(name=="sceRazorCaptureCommandBuffersOnlyImmediate") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceRazorCaptureCommandBuffersOnlyImmediate)>(address));
if(name=="sceRazorCaptureCommandBuffersOnlySinceLastFlip") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceRazorCaptureCommandBuffersOnlySinceLastFlip)>(address));
if(name=="sceGnmDriverTriggerCapture") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmDriverTriggerCapture)>(address));
if(name=="sceGnmDriverCaptureInProgress") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmDriverCaptureInProgress)>(address));
if(name=="sceGnmIsUserPaEnabled") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmIsUserPaEnabled)>(address));
if(name=="sceGnmDispatchInitDefaultHardwareState") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::GnmDriver::sceGnmDispatchInitDefaultHardwareState)>(address));
if(name=="sceVideoOutOpen") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::VideoOut::sceVideoOutOpen)>(address));
if(name=="sceVideoOutSetFlipRate") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::VideoOut::sceVideoOutSetFlipRate)>(address));
if(name=="sceVideoOutAddFlipEvent") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::VideoOut::sceVideoOutAddFlipEvent)>(address));
if(name=="sceVideoOutSetBufferAttribute") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::VideoOut::sceVideoOutSetBufferAttribute)>(address));
if(name=="sceVideoOutClose") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::VideoOut::sceVideoOutClose)>(address));
if(name=="sceVideoOutRegisterBuffers") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::VideoOut::sceVideoOutRegisterBuffers)>(address));
if(name=="sceVideoOutGetFlipStatus") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::VideoOut::sceVideoOutGetFlipStatus)>(address));
if(name=="sceVideoOutSubmitFlip") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::VideoOut::sceVideoOutSubmitFlip)>(address));
if(name=="posix_kqueue") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::Kernel::posix_kqueue)>(address));
if(name=="posix_kevent") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::Kernel::posix_kevent)>(address));
if(name=="sceKernelCreateEqueue") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::Kernel::sceKernelCreateEqueue)>(address));
if(name=="sceKernelDeleteEqueue") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::Kernel::sceKernelDeleteEqueue)>(address));
if(name=="sceKernelWaitEqueue") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::Kernel::sceKernelWaitEqueue)>(address));
if(name=="sceKernelAddHRTimerEvent") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::Kernel::sceKernelAddHRTimerEvent)>(address));
if(name=="sceKernelDeleteHRTimerEvent") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::Kernel::sceKernelDeleteHRTimerEvent)>(address));
if(name=="sceKernelAddTimerEvent") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::Kernel::sceKernelAddTimerEvent)>(address));
if(name=="sceKernelDeleteTimerEvent") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::Kernel::sceKernelDeleteTimerEvent)>(address));
if(name=="sceKernelAddUserEvent") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::Kernel::sceKernelAddUserEvent)>(address));
if(name=="sceKernelAddUserEventEdge") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::Kernel::sceKernelAddUserEventEdge)>(address));
if(name=="sceKernelGetEventUserData") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::Kernel::sceKernelGetEventUserData)>(address));
if(name=="sceKernelGetEventId") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::Kernel::sceKernelGetEventId)>(address));
if(name=="sceKernelTriggerUserEvent") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::Kernel::sceKernelTriggerUserEvent)>(address));
if(name=="sceKernelDeleteUserEvent") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::Kernel::sceKernelDeleteUserEvent)>(address));
if(name=="sceKernelGetEventFilter") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::Kernel::sceKernelGetEventFilter)>(address));
if(name=="sceKernelGetEventData") return MakeHleCallAdapter(reinterpret_cast<decltype(&Libraries::Kernel::sceKernelGetEventData)>(address));
return {};
}
