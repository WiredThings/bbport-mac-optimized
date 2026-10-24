// Private ARM prototype only; called after Shared QA session admission.
// Preserve the existing build-tree module path and admit the unchanged launcher's
// bundled module only beside this actual executable. No separate frontend path.
#pragma once
#include <stddef.h>
#include <string.h>
static inline int bb_arm_private_net_module_path(const char* canonical,const char* executable){
    if(!canonical||!executable)return 0;
    static const char build[]=BB_PROTOTYPE_ROOT "/build/";
    if(!strncmp(canonical,build,sizeof(build)-1))return 1;
    static const char suffix[]="/Contents/Resources/bbport/bin/bb-probe-metal";
    static const char name[]="bb-probe-metal";
    static const char module[]="gpu-net/libbbnet.dylib";
    const size_t n=strlen(executable),m=strlen(canonical);
    if(n<sizeof(suffix)-1||strcmp(executable+n-(sizeof(suffix)-1),suffix))return 0;
    const size_t parent=n-(sizeof(name)-1);
    return m==parent+sizeof(module)-1&&!memcmp(canonical,executable,parent)&&!strcmp(canonical+parent,module);
}
