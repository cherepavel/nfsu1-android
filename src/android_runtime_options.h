#ifndef NFSU_ANDROID_RUNTIME_OPTIONS_H
#define NFSU_ANDROID_RUNTIME_OPTIONS_H

#include <string.h>

/* Empty Android properties select the packaged default. Explicit 0 and
 * malformed values retain the conservative off path for diagnostic rollback. */
static inline int android_runtime_option_enabled(const char *value, int default_on)
{
    return !value || !*value ? !!default_on : !strcmp(value, "1");
}

#endif
