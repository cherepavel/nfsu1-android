/* cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined
 * tests/android_runtime_options.c -o /tmp/nfsu-runtime-options-test */
#include "../src/android_runtime_options.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    /* Clean-install executor default is on; ordinary profiling is off.
     * Diagnostic mode can select on without overriding an explicit rollback. */
    for (int default_on = 0; default_on <= 1; default_on++) {
        assert(android_runtime_option_enabled(NULL, default_on) == default_on);
        assert(android_runtime_option_enabled("", default_on) == default_on);
        assert(android_runtime_option_enabled("1", default_on));
        const char *disabled[] = {"0", "invalid", "true", "01", "-1", " 1", "1 "};
        for (unsigned i = 0; i < sizeof disabled / sizeof disabled[0]; i++)
            assert(!android_runtime_option_enabled(disabled[i], default_on));
    }
    puts("Android runtime options: packaged defaults, opt-in profiling, explicit rollback: PASS");
    return 0;
}
