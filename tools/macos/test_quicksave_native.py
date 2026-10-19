#!/usr/bin/env python3
"""Exercise actual native-runtime debounce, input reservation and save-mount exclusion."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--native-checkout', type=Path, default=ROOT)
args = parser.parse_args()
native = args.native_checkout.resolve()
sdk = os.environ.get('SDKROOT') or subprocess.check_output(['xcrun', '--sdk', 'macosx', '--show-sdk-path'], text=True).strip()

CASES = {
    'session_lock': r'''
#include <assert.h>
#include "runtime_quicksave.c"
static char directory[]="/private/tmp/bb-save-lock-XXXXXX";
int runtime_savedata_quick_action(int load) { (void)load; return 0; }
const char *runtime_file_user_dir(void) { return directory; }
void runtime_restart(void) { assert(0); }
int runtime_memory_is_mapped(uintptr_t p,uint64_t n) { (void)p;(void)n;return 0; }
int main(void) {
    assert(mkdtemp(directory));
    setenv("BB_ONLINE","1",1); setenv("BB_QUICKSAVE","0",1);
    runtime_quicksave_init("CUSA00207");
    assert(!runtime_quicksave_enabled() && runtime_quicksave_execute(1)==-1);
    char path[1024]; snprintf(path,sizeof(path),"%s/.quicksave-session.lock",directory);
    int other=open(path,O_RDWR); assert(other>=0);
    assert(flock(other,LOCK_EX|LOCK_NB)!=0);
    close(other); close(session_fd);
    unlink(path); rmdir(directory);
    puts("PASS: online runtime locks shared saves even when quicksave is disabled");
}
''',

    'online_restore': r'''
#include <assert.h>
#include "runtime_quicksave.c"
static char directory[]="/private/tmp/bb-online-restore-XXXXXX";
static int restarted;
int runtime_savedata_quick_action(int load) { (void)load; return 0; }
const char *runtime_file_user_dir(void) { return directory; }
void runtime_restart(void) { restarted=1; }
int runtime_memory_is_mapped(uintptr_t p,uint64_t n) { (void)p;(void)n;return 0; }
int main(void) {
    assert(mkdtemp(directory));
    setenv("BB_ONLINE","1",1); setenv("BB_QUICKSAVE","1",1);
    setenv("BB_QUICKSAVE_PYTHON","/usr/bin/true",1); setenv("BB_QUICKSAVE_HELPER","/unused",1);
    runtime_quicksave_init("CUSA00207"); assert(runtime_quicksave_enabled());
    setenv("BB_THD_TOKEN","fixture",1); setenv("BB_NET_MODULE","fixture",1);
    setenv("SHADPS4_HTTP_HOST_OVERRIDES_JSON","fixture",1);
    assert(runtime_quicksave_execute(1)==-1 && restarted);
    assert(!strcmp(getenv("BB_ONLINE"),"0") && !strcmp(getenv("BB_QUICKLOAD_PENDING"),"1"));
    assert(!getenv("BB_THD_TOKEN") && !getenv("BB_NET_MODULE") && !getenv("SHADPS4_HTTP_HOST_OVERRIDES_JSON"));
    close(session_fd); char path[1024]; snprintf(path,sizeof(path),"%s/.quicksave-session.lock",directory);
    unlink(path); rmdir(directory);
    puts("PASS: online checkpoints enabled; every quickload strips credentials before restarting offline");
}
''',

    'control': r'''
#include <assert.h>
#include "runtime_quicksave.c"
static int called, operation, allow=1;
int runtime_savedata_quick_action(int load) { ++called; operation=load; return allow; }
const char *runtime_file_user_dir(void) { return "/unused"; }
void runtime_restart(void) { assert(0); }
int runtime_memory_is_mapped(uintptr_t p,uint64_t n) { (void)p;(void)n;return 0; }
int main(void) {
    enabled=1;
    runtime_quicksave_keys(0,0,0);
    runtime_quicksave_keys(1,0,0); assert(called==1 && !operation && !pending);
    for (int i=0;i<100;++i) runtime_quicksave_keys(1,0,0);
    assert(called==1); /* held S is one action */
    runtime_quicksave_keys(0,0,0);
    runtime_quicksave_keys(0,1,1); /* text/menu or modifier held */
    runtime_quicksave_keys(0,1,0); assert(called==1);
    runtime_quicksave_keys(0,0,0);
    runtime_quicksave_keys(0,1,0); assert(called==2 && operation);
    runtime_quicksave_keys(0,0,0);
    runtime_quicksave_keys(1,1,0); assert(called==2); /* ambiguous chord */
    runtime_quicksave_keys(0,0,0);
    allow=0;
    runtime_quicksave_keys(1,0,0); assert(pending==1);
    allow=1;
    runtime_quicksave_keys(0,0,0); assert(!pending); /* retry after writes close */
    auto_confirm=1;
    assert(runtime_quicksave_autoconfirm()==0);
    runtime_quicksave_request(1); assert(!pending);
    runtime_quicksave_loaded(); assert(runtime_quicksave_autoconfirm()==0);
    auto_start=clock_ms()-90001;
    assert(runtime_quicksave_autoconfirm()==-1); /* bounded even for unknown builds */
    allow=0;
    runtime_quicksave_keys(0,0,0);
    runtime_quicksave_keys(1,0,0); assert(pending==1);
    requested_at=clock_ms()-16000;
    runtime_quicksave_keys(0,0,0); assert(!pending);
    puts("PASS: S/L edge triggering, held keys, modifiers/text capture, deferred save, autoload release");
}
''',
    'autoload': r'''
#include <assert.h>
#include "runtime_quicksave.c"
static unsigned char image[0x5578a40], manager[0x1579];
static int mapped=1;
int runtime_savedata_quick_action(int load) { (void)load; return 0; }
const char *runtime_file_user_dir(void) { return "/unused"; }
void runtime_restart(void) { assert(0); }
int runtime_memory_is_mapped(uintptr_t p,uint64_t n) {
    return mapped && p==(uintptr_t)manager && n<=sizeof(manager);
}
static ABI void save_request(void) { assert(0); }
int main(void) {
    enabled=1; guest_image=image; game_save=save_request; auto_confirm=1;
    uintptr_t context=1, ptr=(uintptr_t)manager;
    memcpy(image+0x553b0c8,&context,sizeof(context));
    memcpy(image+0x5556678,&ptr,sizeof(ptr));
    /* Profile writable mounts happen before Continue; neither those mounts nor
     * profile commits may cancel the remaining menu confirmations. */
    runtime_quicksave_loaded(); assert(auto_confirm);
    runtime_quicksave_committed("/savedata0/userdata0010");
    runtime_quicksave_loaded(); assert(auto_confirm);
    auto_start=clock_ms()-2401;
    assert(runtime_quicksave_autoconfirm()==1); /* Continue is still pressed */
    manager[0x1578]=1; mapped=0;
    assert(runtime_quicksave_autoconfirm()==1); /* unmapped pointer never read */
    mapped=1;
    assert(runtime_quicksave_autoconfirm()==-1); /* release in the middle of a tap */
    assert(!auto_confirm); /* never rearm during later autosaves/deaths */
    manager[0x1578]=0;
    assert(runtime_quicksave_autoconfirm()==-1);
    puts("PASS: profile mount cannot cancel Continue; verified character readiness releases input once");
}
''',
    'freshness': r'''
#include <assert.h>
#include "runtime_quicksave.c"
static unsigned char image[0x5578a40], manager[0x1579];
static int requests;
int runtime_savedata_quick_action(int load) { (void)load;return 0; }
const char *runtime_file_user_dir(void) { return "/unused"; }
void runtime_restart(void) { assert(0); }
int runtime_memory_is_mapped(uintptr_t p,uint64_t n) {
    return p==(uintptr_t)manager && n<=sizeof(manager);
}
static ABI void save_request(void) { ++requests; }
int main(void) {
    enabled=1;
    runtime_quicksave_bind(image,10); assert(!game_save);
    runtime_quicksave_bind(image,sizeof(image)); assert(!game_save);
    assert(runtime_quicksave_execute(0)==-1); /* unsupported builds fail closed */
    guest_image=image; game_save=save_request;
    assert(runtime_quicksave_execute(0)==0 && !requests); /* menu/not ready */
    uintptr_t context=1, ptr=(uintptr_t)manager;
    memcpy(image+0x553b0c8,&context,sizeof(context));
    memcpy(image+0x5556678,&ptr,sizeof(ptr));
    manager[0x1578]=1;
    /* Use a successful stand-in helper; only the freshness gate is under test. */
    setenv("BB_QUICKSAVE_PYTHON","/usr/bin/true",1);
    setenv("BB_QUICKSAVE_HELPER","/unused",1);
    for (int i=0;i<2;++i) {
        fresh_requested=0;
        runtime_quicksave_committed("/savedata0/userdata0000");
        runtime_quicksave_committed("/savedata0/userdata0010");
        assert(runtime_quicksave_execute(0)==0 && requests==i+1);
        assert(runtime_quicksave_execute(0)==0); /* old writes never qualify */
        runtime_quicksave_committed("/savedata0/userdata0010");
        assert(runtime_quicksave_execute(0)==0); /* profile alone is insufficient */
        runtime_quicksave_committed("/savedata0/userdata0000.bbtmp");
        assert(runtime_quicksave_execute(0)==0);
        runtime_quicksave_committed("/savedata0/userdata0007");
        assert(runtime_quicksave_execute(0)==1); /* supports any character slot */
    }
    fresh_requested=0;
    assert(!runtime_quicksave_execute(0));
    runtime_quicksave_committed("/savedata0/userdata0000");
    assert(!runtime_quicksave_execute(0)); /* wait between the two save mounts */
    runtime_quicksave_committed("/savedata0/userdata0010");
    assert(runtime_quicksave_execute(0)==1);
    puts("PASS: fresh game request, two successive saves, both commits required, unsupported builds rejected");
}
''',
    'mounts': r'''
#define _GNU_SOURCE
#include <assert.h>
#include "runtime_savedata.c"
static int calls, idle=1;
int runtime_quicksave_execute(int load) { ++calls; return load ? -1 : 1; }
void runtime_quicksave_init(const char *title) { (void)title; }
void runtime_quicksave_loaded(void) {}
int runtime_file_savedata_idle(void) { return idle; }
int runtime_file_mount(const char *g,const char *h) { (void)g;(void)h;return 0; }
void runtime_file_unmount(const char *p) { (void)p; }
const char *runtime_file_user_dir(void) { return "/unused"; }
uintptr_t runtime_lookup(const RuntimeExport *e,size_t n,const char *s) { (void)e;(void)n;(void)s;return 0; }
int main(void) {
    slots[5].used=1;
    assert(!runtime_savedata_quick_action(0) && !calls);
    slots[5].used=0; idle=0;
    assert(!runtime_savedata_quick_action(0) && !calls);
    idle=1;
    pthread_mutex_lock(&lock);
    assert(!runtime_savedata_quick_action(0) && !calls);
    pthread_mutex_unlock(&lock);
    assert(runtime_savedata_quick_action(0)==1 && calls==1);
    assert(runtime_savedata_quick_action(1)==-1 && calls==2);
    assert(!pthread_mutex_trylock(&lock)); pthread_mutex_unlock(&lock);
    puts("PASS: mounted saves/open files exclude snapshots; busy lock defers; errors release lock");
}
''',
    'bindings': r'''
#define _GNU_SOURCE
#include <assert.h>
#include "runtime_pad.c"
static int quick=1, seen_save, seen_load, seen_blocked, captured;
static bool host_keys[SDL_SCANCODE_COUNT];
int runtime_quicksave_enabled(void) { return quick; }
void runtime_quicksave_keys(int s,int l,int b) { seen_save=s;seen_load=l;seen_blocked=b; }
void runtime_quicksave_request(int load) { (void)load; }
int runtime_quicksave_autoconfirm(void) { return -1; }
int bbgpu_overlay_captures_input(void) { return captured; }
const bool *bbgpu_keyboard_state(void) { return host_keys; }
uintptr_t runtime_lookup(const RuntimeExport *e,size_t n,const char *s) { (void)e;(void)n;(void)s;return 0; }
int main(void) {
    bool keys[SDL_SCANCODE_COUNT]={0};
    unsetenv("BB_QUICKSAVE_SCANCODE"); unsetenv("BB_QUICKLOAD_SCANCODE");
    unsetenv("BB_MENU_SCANCODE");
    unsetenv("BB_FRAME_STATS"); unsetenv("BB_CONFIG");
    bind_defaults();
    keys[SDL_SCANCODE_S]=keys[SDL_SCANCODE_L]=true;
    assert(!key_down(keys,IN_MOVE_DOWN) && !key_down(keys,IN_RIGHT));
    quick=0;
    assert(key_down(keys,IN_MOVE_DOWN) && key_down(keys,IN_RIGHT));
    quick=1; keys[SDL_SCANCODE_W]=true;
    assert(key_down(keys,IN_MOVE_UP));
    assert(bindings[IN_R1].pad[0]==SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    setenv("BB_QUICKSAVE_SCANCODE","59",1); /* F2 */
    setenv("BB_QUICKLOAD_SCANCODE","60",1); /* F3 */
    bind_defaults();
    assert(quick_bindings.save==SDL_SCANCODE_F2 && quick_bindings.load==SDL_SCANCODE_F3);
    assert(key_down(keys,IN_MOVE_DOWN) && key_down(keys,IN_RIGHT)); /* S/L restored */
    sdl_ready=-1; bindings_ready=0; /* no device initialization needed */
    PadData data;
    host_keys[SDL_SCANCODE_F2]=true;
    sample_host(&data); assert(seen_save && !seen_load && !seen_blocked);
    host_keys[SDL_SCANCODE_F2]=false; host_keys[SDL_SCANCODE_F3]=true;
    sample_host(&data); assert(!seen_save && seen_load && !seen_blocked);
    captured=1; sample_host(&data); assert(seen_blocked);
    captured=0; host_keys[SDL_SCANCODE_LCTRL]=true;
    sample_host(&data); assert(seen_blocked);
    host_keys[SDL_SCANCODE_LCTRL]=false;
    setenv("BB_QUICKSAVE_SCANCODE","58",1); /* F1 must remain the menu */
    bind_defaults(); assert(quick_bindings.save==SDL_SCANCODE_UNKNOWN);
    for (const char **p=(const char*[]){"66","53","73","41","100","-1","99999999999999999999999","garbage",NULL};*p;++p) {
        setenv("BB_QUICKSAVE_SCANCODE",*p,1);
        bind_defaults(); assert(quick_bindings.save==SDL_SCANCODE_UNKNOWN);
    }
    setenv("BB_MENU_SCANCODE","61",1); /* move menu to F4, freeing all legacy aliases */
    SDL_KeyboardEvent event={0};
    event.scancode=SDL_SCANCODE_F4;
    assert(menu_shortcut_matches(&event));
    event.mod=SDL_KMOD_CTRL;
    assert(!menu_shortcut_matches(&event));
    event.mod=0; event.scancode=SDL_SCANCODE_F1;
    assert(!menu_shortcut_matches(&event));
    for (const char **p=(const char*[]){"58","53","73","100",NULL};*p;++p) {
        setenv("BB_QUICKSAVE_SCANCODE",*p,1);
        bind_defaults(); assert(quick_bindings.save!=SDL_SCANCODE_UNKNOWN);
    }
    setenv("BB_QUICKSAVE_SCANCODE","61",1);
    bind_defaults(); assert(!quick_bindings.save);
    setenv("BB_QUICKSAVE_SCANCODE","58",1);
    memset(host_keys,0,sizeof(host_keys)); host_keys[SDL_SCANCODE_F1]=true;
    bindings_ready=0; sample_host(&data); assert(seen_save && !seen_load && !seen_blocked);
    setenv("BB_MENU_SCANCODE","garbage",1); /* invalid menu safely falls back to F1 */
    bind_defaults(); assert(!quick_bindings.save);
    setenv("BB_MENU_SCANCODE","61",1);
    setenv("BB_QUICKSAVE_SCANCODE","60",1); /* same as load */
    bind_defaults(); assert(!quick_bindings.save && !quick_bindings.load);
    setenv("BB_QUICKSAVE_SCANCODE","26",1); /* W is already movement */
    bindings_ready=0; sample_host(&data); assert(!quick_bindings.save);
    puts("PASS: remapped menu/save/load routing, old keys freed, conflicts rejected, menu/modifier blocking");
}
''',
}

with tempfile.TemporaryDirectory(prefix='bb-quicksave-test-') as temp:
    for name, source in CASES.items():
        c = Path(temp) / (name + '.c')
        exe = Path(temp) / name
        c.write_text(source)
        command = ['clang', '-arch', 'x86_64', '-isysroot', sdk, '-std=c11', '-O1',
                   '-Wall', '-Wextra', '-Werror', '-pthread', '-I' + str(native),
                   '-I' + str(native / 'src'), str(c), '-o', str(exe)]
        if name == 'bindings':
            deps = Path(os.environ.get('BB_METAL_DEPS', native / 'deps-x86_64'))
            command += ['-I' + str(deps / 'include'), '-L' + str(deps / 'lib'),
                        '-Wl,-rpath,' + str(deps / 'lib'), '-lSDL3.0']
        subprocess.run(command, check=True)
        subprocess.run([str(exe)], check=True)
