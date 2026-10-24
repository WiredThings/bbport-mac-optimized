#pragma once
// Isolated startup-only experiment. The caller must own an unpublished cache and
// prove that no guest execution/publication can race this operation. Used only by the explicit private startup-cache experiment.
#include "bb_cpu_cache_validation.h"
#include "Interface/Context/Context.h"
#include "Interface/Core/JIT/Relocations.h"
#include <FEXCore/Utils/AllocatorHooks.h>
namespace BbEagerCache {
inline bool Prepare(FEXCore::Context::ContextImpl& context,FEXCore::MappedCodeCacheFile& file,
                    const BbCacheValidation::Validated& checked){
#if defined(__APPLE__) && !TARGET_OS_IPHONE
 auto& cache=static_cast<FEXCore::Context::CodeCache&>(context.GetCodeCache());
 // FEX's lazy flag suppresses its RX-remap finalizer. This helper eagerly loads
 // EVERY byte before lookup publication; no fault-driven or mixed-state paging.
 if(file.CacheManager!=&cache||(!cache.ExternalStartupConsumption&&!cache.EnableLazyCodeCaching())||cache.EnableCodeCacheValidation()||
    file.CodeBuffer.size()!=checked.code_bytes||file.CodeBufferInFile.size()!=checked.code_bytes||
    checked.code_offset>file.MappedFile.size()||file.MappedFile.size()-checked.code_offset<checked.code_bytes||
    file.CodeBufferInFile.data()!=file.MappedFile.data()+checked.code_offset||
    file.LoadedPages.size()!=checked.code_bytes/4096||
    std::any_of(file.LoadedPages.begin(),file.LoadedPages.end(),[](bool ready){return ready;})||
    checked.relocation_offset>checked.code_offset||
    checked.relocs.size()>(checked.code_offset-checked.relocation_offset)/sizeof(FEXCore::CPU::Relocation)||
    cache.IsAddressInMappedCodeBuffer(reinterpret_cast<uintptr_t>(file.CodeBuffer.data())))return false;
 void* storage=FEXCore::Allocator::VirtualAlloc(checked.code_bytes,true);
 if(!storage||storage==MAP_FAILED)return false;
 bool applied=false;
 {
  FEXCore::Allocator::ScopedJITWriteProtect write;
  std::memcpy(storage,file.CodeBufferInFile.data(),checked.code_bytes);
  auto records=std::span(reinterpret_cast<const FEXCore::CPU::Relocation*>(file.MappedFile.data()+checked.relocation_offset),checked.relocs.size());
  applied=cache.ApplyCodeRelocations(file.GuestBase,{static_cast<std::byte*>(storage),checked.code_bytes},records,0,false);
 }
 if(!applied){FEXCore::Allocator::VirtualFree(storage,checked.code_bytes);return false;}
 __builtin___clear_cache(static_cast<char*>(storage),static_cast<char*>(storage)+checked.code_bytes);
 const auto old=file.CodeBuffer;
 file.CodeBuffer={static_cast<std::byte*>(storage),checked.code_bytes};
 std::fill(file.LoadedPages.begin(),file.LoadedPages.end(),true);
 FEXCore::Allocator::VirtualFree(old.data(),old.size());
 return true;
#else
 (void)context;(void)file;(void)checked;return false;
#endif
}
}
