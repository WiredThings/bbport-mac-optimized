// SPDX-License-Identifier: MIT
// Native Darwin queue hand-off: a waiting reader must not consume the sole
// wake meant for a queued writer. No guest/game content or GPU allocation.
#ifdef BB_OLD_WRITER_MUTEX
#include "writer_mutex_original.h"
#else
#include <FEXCore/Utils/WritePriorityMutex.h>
#endif
#include <atomic>
#include <thread>
#include <type_traits>
#include <unistd.h>
#include <signal.h>
#include <cstdio>
#include <cstdlib>
using Mutex=FEXCore::Utils::WritePriorityMutex::Mutex;
static_assert(std::is_standard_layout_v<Mutex> && sizeof(Mutex)==sizeof(uint32_t));
static void timed_out(int){const char message[]="WRITER HANDOFF stalled: queued reader consumed writer wake\n";write(2,message,sizeof(message)-1);_Exit(7);}
static uint32_t state(Mutex& m){return std::atomic_ref<uint32_t>(*reinterpret_cast<uint32_t*>(&m)).load(std::memory_order_acquire);}
int main(){signal(SIGALRM,timed_out);alarm(8);
 for(unsigned round=0;round<100;++round){
  Mutex m;std::atomic<unsigned> order{0};m.lock();
  std::thread reader([&]{m.lock_shared();if(order.load()!=1)abort();m.unlock_shared();});
  while(!(state(m)&(1u<<30)))std::this_thread::yield();usleep(1000);
  std::thread writer([&]{m.lock();order.store(1);m.unlock();});
  while(!(state(m)&(0x3fffu<<16)))std::this_thread::yield();usleep(1000);
  m.unlock();writer.join();reader.join();if(order.load()!=1)abort();
 }
 alarm(0);puts("WRITER HANDOFF 100 queued reader/writer exchanges completed");
}
