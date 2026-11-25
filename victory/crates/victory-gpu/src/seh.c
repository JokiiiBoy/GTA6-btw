#ifdef _WIN32
#include <windows.h>

int victory_seh_call(int (*func)(void *), void *ctx) {
    __try {
        return func(ctx);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return -2147483648; /* sentinel for SEH exception */
    }
}
#endif
