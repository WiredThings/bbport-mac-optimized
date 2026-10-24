/* macOS restart handshake. Keep Cocoa's old process registration out of the new game. */
#ifndef BB_RUNTIME_RESTART_PROTOCOL_H
#define BB_RUNTIME_RESTART_PROTOCOL_H
#include <stdlib.h>
#include <string.h>

enum { BB_RESTART_EXIT = 75, BB_QUICKLOAD_EXIT = 76 };

static inline int runtime_supervised_restart_status(void) {
    const char *supervised = getenv("BB_RESTART_SUPERVISED");
    if (!supervised || strcmp(supervised, "1")) return 0;
    const char *loading = getenv("BB_QUICKLOAD_PENDING");
    return loading && !strcmp(loading, "1") ? BB_QUICKLOAD_EXIT : BB_RESTART_EXIT;
}
#endif
