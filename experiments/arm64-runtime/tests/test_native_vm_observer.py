"""Real SDK layout and read-only kernel API; no game or preferences."""
from pathlib import Path
import ctypes as C,json,subprocess,sys,tempfile
P=Path(__file__).resolve().parents[1];sys.path.insert(0,str(P/'tools'))
from native_vm_observer import VMRev1,NativeVMObserver
with tempfile.TemporaryDirectory(prefix='bb-vm-layout-')as root:
    src=Path(root)/'check.c';exe=Path(root)/'check'
    fields=[n for n,_ in VMRev1._fields_]
    src.write_text('#include <stddef.h>\n#include <stdio.h>\n#include <mach/host_info.h>\nint main(void){printf("%zu\\n",(size_t)HOST_VM_INFO64_REV1_COUNT);'+''.join('printf("%zu\\n",offsetof(vm_statistics64_data_t,'+n+'));'for n in fields)+'return 0;}')
    subprocess.run(['clang','-arch','arm64',str(src),'-o',str(exe)],check=True)
    values=[int(n)for n in subprocess.check_output([str(exe)],text=True).splitlines()]
    assert values[0]*4==C.sizeof(VMRev1),(values[0],C.sizeof(VMRev1))
    assert values[1:]==[getattr(VMRev1,n).offset for n in fields]
observer=NativeVMObserver();a=observer.system();b=observer.system();observer.close()
for key in ['pageins','pageouts','faults','decompressions','compressions','swapins','swapouts']:assert b[key]>=a[key],key
assert set(a['cpu_ticks'])=={'user','system','idle','nice'}
assert a['end_mono_ns']>=a['start_mono_ns']and a['scope']=='system-wide'
try:observer.system()
except RuntimeError:pass
else:raise AssertionError('closed port was reused')
print(json.dumps(dict(passed=True,SDK_prefix_bytes=C.sizeof(VMRev1),SDK_fields=len(fields),kernel_words=a['kernel_words'],page_bytes=a['page_bytes'],game_started=False)))
