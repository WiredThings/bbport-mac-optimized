/* Checkpoint controls, opt-in with BB_QUICKSAVE=1. Copy only outside all save mounts;
 * restore before the guest starts, using the port's normal restart mechanism. */
#define _GNU_SOURCE
#include "runtime.h"
#include "runtime_quicksave.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
static int enabled, pending, previous_keys=3;
/* Private World Warper needs menu input/readiness, never checkpoint operations. */
static int private_auto_continue(void) {
    const char *dir=getenv("BB_DEV_WORLD_WARPER_DIR"), *auto_input=getenv("BB_ARM_AUTO_CONTINUE");
    return dir && *dir && auto_input && !strcmp(auto_input,"1");
}
static int session_fd=-1;
static char game_title[16];
static uint64_t requested_at, auto_start;
static int auto_confirm;
static unsigned char *guest_image;
static GuestCallback game_save;
static uint64_t character_commits, profile_commits, character_before, profile_before;
static int fresh_requested;
/* Bloodborne 1.09's no-argument script SaveRequest queues a character/profile
 * save on the normal save worker. Validate its entire body, including the
 * RIP-relative singleton references, before using version-specific offsets. */
void runtime_quicksave_bind(void *image, uint64_t size) {
    guest_image=NULL; game_save=NULL;
    if ((!enabled && !private_auto_continue()) || size<0x5578a40) return;
    const unsigned char *code=(const unsigned char *)image+0x1336da0;
    uint64_t hash=UINT64_C(14695981039346656037);
    for (size_t i=0;i<116;++i) hash=(hash^code[i])*UINT64_C(1099511628211);
    if (hash!=UINT64_C(0x1747c21173537f4d)) {
        puts("Quicksave: unrecognized SaveRequest; fresh checkpoints disabled");
        return;
    }
    guest_image=image; game_save=(GuestCallback)code;
    puts("Quicksave: verified Bloodborne 1.09 SaveRequest");
}
void runtime_quicksave_committed(const char *path) {
    if (!enabled) return;
    const char *name=strrchr(path,'/');
    if (!name) return;
    ++name;
    if (strlen(name)==12 && !strncmp(name,"userdata000",11) && name[11]>='0' && name[11]<='9')
        __atomic_add_fetch(&character_commits,1,__ATOMIC_RELEASE);
    else if (!strcmp(name,"userdata0010"))
        __atomic_add_fetch(&profile_commits,1,__ATOMIC_RELEASE);
}
/* This flag is the guest's own SaveRequest readiness gate, not a host save
 * mount. The title screen also mounts/writes the profile before Continue. */
static int character_ready(void) {
    if (!guest_image || !game_save) return 0;
    uintptr_t context=0, manager=0;
    memcpy(&context,guest_image+0x553b0c8,sizeof(context));
    memcpy(&manager,guest_image+0x5556678,sizeof(manager));
    if (!context || !manager || !runtime_memory_is_mapped(manager,0x1579) ||
        !*(volatile unsigned char *)(manager+0x1578)) return 0;
    return 1;
}
static int request_fresh_save(void) {
    if (!game_save) return -1;
    if (!character_ready()) return 0;
    character_before=__atomic_load_n(&character_commits,__ATOMIC_ACQUIRE);
    profile_before=__atomic_load_n(&profile_commits,__ATOMIC_ACQUIRE);
    /* Called on the guest input thread, with no live save mount. The verified
     * function sets request flags; the guest performs serialization itself. */
    bb_arm_call((uintptr_t)game_save,NULL,0);
    fresh_requested=1;
    puts("Quicksave: requested a fresh character and profile save");
    return 1;
}
static uint64_t clock_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000u+(uint64_t)t.tv_nsec/1000000u;
}
static void message(const char *text) {
    printf("Quicksave: %s\n",text);
    const char *path=getenv("BB_QUICKSAVE_STATUS");
    if (!path || !*path) return;
    char temp[4096];
    if (snprintf(temp,sizeof(temp),"%s.tmp",path)>=(int)sizeof(temp)) return;
    FILE *f=fopen(temp,"w");
    if (!f) return;
    fprintf(f,"%lld\n%s\n",(long long)time(NULL)+8,text);
    if (!fclose(f)) rename(temp,path);
}
static int helper(const char *action) {
    const char *python=getenv("BB_QUICKSAVE_PYTHON"), *script=getenv("BB_QUICKSAVE_HELPER");
    if (!python || !script) return -1;
    char *args[]={(char *)python,(char *)script,(char *)action,
                  (char *)runtime_file_user_dir(),game_title,NULL};
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
#ifdef __APPLE__
    posix_spawnattr_setflags(&attr,POSIX_SPAWN_CLOEXEC_DEFAULT);
#endif
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    /* CLOEXEC_DEFAULT also covers the standard streams unless explicitly inherited. */
    for (int fd=0;fd<3;++fd) posix_spawn_file_actions_adddup2(&actions,fd,fd);
    pid_t pid;
    int result=posix_spawn(&pid,python,&actions,&attr,args,environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    if (result) { message("Save helper could not start; progress unchanged"); return -1; }
    const uint64_t deadline=clock_ms()+30000;
    int status;
    for (;;) {
        pid_t done=waitpid(pid,&status,WNOHANG);
        if (done==pid) return WIFEXITED(status) && !WEXITSTATUS(status) ? 0 : -1;
        if (done<0 && errno!=EINTR) return -1;
        if (clock_ms()>deadline) {
            kill(pid,SIGKILL); while (waitpid(pid,&status,0)<0 && errno==EINTR) {}
            message("Save operation timed out; checkpoint recovery will run next launch");
            return -1;
        }
        usleep(10000);
    }
}
int runtime_quicksave_enabled(void) { return enabled; }
void runtime_quicksave_init(const char *title) {
    const char *on=getenv("BB_QUICKSAVE"), *restore=getenv("BB_QUICKLOAD_PENDING");
    const int loading=restore && !strcmp(restore,"1");
    unsetenv("BB_QUICKLOAD_PENDING");
    /* One save lock in both modes. A restore may only run in a new offline host. */
    const char *online=getenv("BB_ONLINE");
    const int active=on && !strcmp(on,"1");
    if (loading && (!active || (online && !strcmp(online,"1")))) {
        message("Quickload requires an offline restart; game launch stopped"); exit(1);
    }
    snprintf(game_title,sizeof(game_title),"%s",title);
    char path[1024];
    snprintf(path,sizeof(path),"%s/.quicksave-session.lock",runtime_file_user_dir());
    session_fd=open(path,O_RDWR|O_CREAT|O_CLOEXEC,0600);
    if (session_fd<0 || flock(session_fd,LOCK_EX|LOCK_NB)) {
        message("Another game is using these saves; close it before launching again");
        exit(1);
    }
    if (!active) return;
    if (helper("recover")) { message("Save recovery failed; game launch stopped"); exit(1); }
    if (loading && helper("load")) { message("Quickload failed; game launch stopped to protect saves"); exit(1); }
    enabled=1;
    if (loading) { auto_start=0; __atomic_store_n(&auto_confirm,1,__ATOMIC_RELEASE); }
    puts("Quicksave: enabled; keyboard shortcuts are configured by the launcher");
}
int runtime_quicksave_execute(int load) {
    if (!enabled) return -1;
    if (!load) {
        if (!fresh_requested) {
            if (request_fresh_save()<0) {
                message("Fresh save unavailable for this game build; checkpoint unchanged");
                return -1;
            }
            return 0;
        }
        /* Both files must be newly committed: they use separate save mounts. A
         * previous autosave, read, failed write or profile-only save cannot
         * satisfy this request. Our caller holds the save mutex throughout. */
        if (__atomic_load_n(&character_commits,__ATOMIC_ACQUIRE)<=character_before ||
            __atomic_load_n(&profile_commits,__ATOMIC_ACQUIRE)<=profile_before) return 0;
    }
    if (helper(load ? "check" : "save")) return -1;
    if (load) {
        message("Loading quicksave offline...");
        if (setenv("BB_QUICKLOAD_PENDING","1",1) || setenv("BB_ONLINE","0",1) ||
            setenv("BB_SKIP_NETWORK_CHOICE","1",1)) return -1;
        // Also protect the direct-exec fallback; the supervised parent repeats this.
        const char *keys[]={"BB_NET_MODULE","BB_NET_BACKEND","BB_THD_TOKEN","BB_THD_ONLINE_ID",
            "BB_THD_SERVER","BB_THD_AUTH","BB_THD_HTTP_ORIGIN","BB_THD_HTTP_ROUTE","BB_THD_STUN",
            "BB_THD_RULESET","BB_P2P_PORT","BB_COMMUNITY_SERVER","BB_SHADNET_SERVER","BB_SHADNET_WEBAPI",
            "BB_SHADNET_NPID","BB_SHADNET_PASSWORD","BB_SHADNET_TOKEN","SHADPS4_HTTP_HOST_OVERRIDES_JSON"};
        for (size_t i=0;i<sizeof(keys)/sizeof(keys[0]);++i) unsetenv(keys[i]);
        runtime_restart(); /* does not return; closes the session lock before exec */
        return -1;
    }
    return 1;
}
void runtime_quicksave_request(int load) {
    if (!enabled || pending || __atomic_load_n(&auto_confirm,__ATOMIC_ACQUIRE)) return;
    pending=load ? 2 : 1;
    requested_at=clock_ms();
    fresh_requested=0;
    message(load ? "Preparing quickload..." : "Saving checkpoint...");
}
void runtime_quicksave_keys(int save, int load, int blocked) {
    if (!enabled) return;
    const int keys=(save ? 1 : 0)|(load ? 2 : 0);
    const int pressed=keys & ~previous_keys;
    previous_keys=keys;
    if (!blocked && keys!=3) {
        if (pressed&2) runtime_quicksave_request(1);
        else if (pressed&1) runtime_quicksave_request(0);
    }
    if (!pending || blocked) return;
    const int result=runtime_savedata_quick_action(pending==2);
    if (result) pending=0;
    else if (clock_ms()-requested_at>15000) {
        pending=0;
        message("Game could not finish saving; checkpoint unchanged - try saving again in gameplay");
    }
}
/* Private runner input automation for a normal-save startup; never restores a checkpoint. */
void runtime_quicksave_begin_auto_continue(void) {
    if(enabled || private_auto_continue()){auto_start=0;__atomic_store_n(&auto_confirm,1,__ATOMIC_RELEASE);}
}
void runtime_quicksave_loaded(void) {
    if (!__atomic_load_n(&auto_confirm,__ATOMIC_ACQUIRE) || !character_ready()) return;
    if (__atomic_exchange_n(&auto_confirm,0,__ATOMIC_ACQ_REL))
        puts("Quicksave: character ready; automatic menu input released");
}
int runtime_quicksave_autoconfirm(void) {
    if (!__atomic_load_n(&auto_confirm,__ATOMIC_ACQUIRE)) return -1;
    /* Poll as well as checking mounts: the final mount can precede readiness.
     * Check before generating Cross, including when readiness changes mid-tap. */
    runtime_quicksave_loaded();
    if (!__atomic_load_n(&auto_confirm,__ATOMIC_ACQUIRE)) return -1;
    const uint64_t now=clock_ms();
    if (!auto_start) auto_start=now;
    if (now-auto_start>90000) {
        __atomic_store_n(&auto_confirm,0,__ATOMIC_RELEASE);
        message("Quicksave restored - select Continue to finish loading");
        return -1;
    }
    /* A short Cross tap advances Offline/Continue. Profile mounts cannot stop it;
     * the verified guest readiness gate releases input before the next tap. */
    return now-auto_start>2000 && ((now-auto_start)%1200)<160 ? 1 : 0;
}
#else
int runtime_quicksave_enabled(void) { return 0; }
void runtime_quicksave_init(const char *title) { (void)title; }
void runtime_quicksave_keys(int s,int l,int b) { (void)s;(void)l;(void)b; }
void runtime_quicksave_request(int load) { (void)load; }
int runtime_quicksave_execute(int load) { (void)load; return -1; }
void runtime_quicksave_bind(void *image,uint64_t size) { (void)image;(void)size; }
void runtime_quicksave_committed(const char *path) { (void)path; }
void runtime_quicksave_loaded(void) {}
int runtime_quicksave_autoconfirm(void) { return -1; }
#endif
