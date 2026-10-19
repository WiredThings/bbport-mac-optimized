// SPDX-License-Identifier: GPL-2.0-or-later
// Dynamic-module boundary for the vendored networking bridge; no renderer linkage.
#include "bbnet.h"
#include "common/path_util.h"
#include "common/thread.h"
#include "core/libraries/libs.h"
#include "thd/thd.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <pthread.h>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

extern "C" { volatile int runtime_restarting = 0; }

namespace {
BbNetHost host{};
struct Symbol { std::string nid; uintptr_t address; };
std::vector<Symbol> symbols;
}
extern "C" void bbnet_configure(const char*, const char*, const char*, const char*);
extern "C" void bbnet_register();
extern "C" void bbnet_initialize();
extern "C" void bbnet_set_image(unsigned char*, uint64_t);

namespace Core::Loader {
void SymbolsResolver::AddSymbol(const char* nid, const char*, const char*, SymbolType, u64 address) {
    symbols.push_back({nid, uintptr_t(address)});
}
}
extern "C" int32_t runtime_thread_spawn(void** out, void* PS4_SYSV_ABI (*entry)(void*),
                                        void* arg, uint64_t stack, const char* name) {
    return host.thread_spawn(out, entry, arg, stack, name);
}
extern "C" int32_t runtime_thread_join_spawned(void* thread, void** result) {
    return host.thread_join(thread, result);
}
extern "C" [[noreturn]] void runtime_thread_exit(void* result) {
    host.thread_exit(result);
    std::abort();
}
extern "C" int runtime_memory_is_mapped(uintptr_t address, uint64_t size) {
    return size && address <= UINTPTR_MAX - size && host.memory_is_mapped(address, size);
}
namespace Common {
void SetCurrentThreadName(const char* name) { pthread_setname_np(name); }
std::string GetCurrentThreadName() {
    char name[64]{};
    pthread_getname_np(pthread_self(), name, sizeof(name));
    return name;
}
namespace FS {
const std::filesystem::path& GetUserPath(PathType) {
    // An online-only module never discovers or creates a default formal-save location.
    static const std::filesystem::path path = [] {
        const char* value = std::getenv("BB_USER_DIR");
        return value ? std::filesystem::path(value) : std::filesystem::path();
    }();
    return path;
}
}
}
namespace Libraries::Kernel {
int* PS4_SYSV_ABI __Error() { return host.errno_location(); }
s32 PS4_SYSV_ABI sceKernelUsleep(u32 microseconds) {
    std::this_thread::sleep_for(std::chrono::microseconds(microseconds));
    return 0;
}
u64 PS4_SYSV_ABI sceKernelGetProcessTime() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
s32 PS4_SYSV_ABI sceKernelGetCompiledSdkVersion(s32* version) {
    if (!version) return s32(0x8002000e);
    *version = s32(host.sdk_version);
    return 0;
}
}
extern "C" __attribute__((visibility("default"))) uintptr_t bbnet_resolve(const char* nid) {
    if (!nid) return 0;
    const size_t length = std::strcspn(nid, "#");
    for (const auto& item : symbols)
        if (item.nid.size() == length && !std::memcmp(item.nid.data(), nid, length)) return item.address;
    return 0;
}
extern "C" __attribute__((visibility("default"))) int bbnet_init(const BbNetHost* value) {
    if (!value || value->abi != BBNET_ABI_VERSION || !value->thread_spawn ||
        !value->thread_join || !value->thread_exit || !value->memory_is_mapped ||
        !value->errno_location) return -1;
    if (!symbols.empty()) return -1;
    host = *value;
    bbnet_configure(host.app0, host.serial, host.title, host.app_ver);
    bbnet_set_image(host.image, host.image_size);
    bbnet_register();
    bbnet_initialize();
    std::printf("Online: %zu network functions registered; backend=%s\n", symbols.size(),
                BbThd::Enabled() ? "The Hunter's Dream" : "shadNet");
    return 0;
}
