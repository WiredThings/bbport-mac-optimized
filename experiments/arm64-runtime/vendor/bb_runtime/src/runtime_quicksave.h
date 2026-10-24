#ifndef BB_RUNTIME_QUICKSAVE_H
#define BB_RUNTIME_QUICKSAVE_H
#include <stdint.h>
void runtime_quicksave_bind(void *image, uint64_t size);
void runtime_quicksave_committed(const char *path);
void runtime_quicksave_init(const char *title);
int runtime_quicksave_enabled(void);
void runtime_quicksave_keys(int save, int load, int blocked);
void runtime_quicksave_request(int load);
int runtime_quicksave_execute(int load);
void runtime_quicksave_loaded(void);
int runtime_quicksave_autoconfirm(void); /* -1 normal controls, 0 neutral, 1 Cross */
int runtime_savedata_quick_action(int load); /* 0 wait, 1 completed, -1 failed */
int runtime_file_savedata_idle(void);
#endif
