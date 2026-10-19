from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

@unittest.skipUnless(sys.platform=='darwin', 'macOS dynamic module test')
class OnlineRuntimeTests(unittest.TestCase):
    def test_token_destination_policy_rejects_redirects_outside_trusted_service(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory); source=root/'policy.cpp'; binary=root/'policy'
            source.write_text(r'''
#include "gpu/bbnet/include/bb_thd_http_policy.h"
#include <cassert>
int main() {
 for (unsigned port : {443u, 18671u, 20443u}) {
  assert(BbThdHttpDestination("https", "thehuntersdream.com", port));
  assert(BbThdHttpDestination("https", "TheHuntersDream.com", port));
 }
 assert(!BbThdHttpDestination("http", "thehuntersdream.com", 443));
 assert(!BbThdHttpDestination("https", "thehuntersdream.com", 123));
 assert(!BbThdHttpDestination("https", "thehuntersdream.com.evil.test", 443));
 assert(!BbThdHttpDestination("https", "evil.test", 443));
}
''')
            subprocess.run(['clang++','-std=c++20','-I',str(ROOT),str(source),'-o',str(binary)],check=True,capture_output=True)
            subprocess.run([str(binary)],check=True,capture_output=True)

    def test_offline_load_gate_and_online_host_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            fake=root/'fake.c'
            fake.write_text(r'''
#include "gpu/bbnet/bbnet.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int bbnet_init(const BbNetHost *h){
 assert(h && h->abi==BBNET_ABI_VERSION && h->image_size==16 && h->sdk_version==0x03500000);
 assert(h->thread_spawn && h->thread_join && h->thread_exit && h->memory_is_mapped && h->errno_location);
 assert(!strcmp(h->serial,"CUSA03173") && !strcmp(h->app_ver,"01.09"));
 FILE*f=fopen(getenv("BB_TEST_NET_FILE"),"w");assert(f);fputs("initialized",f);fclose(f);return 0;
}
uintptr_t bbnet_resolve(const char*n){return !strcmp(n,"sceNetInit") ? 0x1234 : 0;}
''')
            module=root/'fake.dylib'; test=root/'test'
            subprocess.run(['clang','-arch','x86_64','-dynamiclib','-I',str(ROOT),str(fake),'-o',str(module)],check=True,capture_output=True)
            subprocess.run(['clang','-arch','x86_64','-I',str(ROOT),'-I',str(ROOT/'src'),
                            str(ROOT/'tests/test_online_runtime.c'),str(ROOT/'src/runtime_net.c'),'-o',str(test)],check=True,capture_output=True)
            for flag in ['0','yes','10','1']:
                marker=root/('marker-'+flag)
                result=subprocess.run([str(test),flag,str(module),str(marker)],capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertEqual(marker.exists(),flag=='1')

if __name__=='__main__': unittest.main()
