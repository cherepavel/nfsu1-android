"""Compile localized GPR/MMX bodies with both register storage models."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from .translator import _scope_register_block_aliases


class RegisterBlockTest(unittest.TestCase):
    def test_local_registers_and_next_function_keep_their_storage(self):
        cc = shutil.which("cc")
        if not cc:
            self.skipTest("no C compiler")
        runtime = Path(__file__).resolve().parents[2] / "templates/runtime"
        body = _scope_register_block_aliases([
            "static void localized(void) {",
            "  uint32_t g_eax = recomp_leaf_ld_eax();",
            "  uint32_t g_esp = recomp_leaf_ld_esp();",
            "  RecompMmx g_mm0 = recomp_leaf_ld_mm0();",
            "  double *const g_fp_stack = recomp_fp_base();",
            "  int g_fp_top = recomp_fp_top_ld();",
            "  eax += 3; esp += 4; mm0.q ^= 0x1234;",
            "  g_xmm2.q[1] = 99;",  # This nonlocalized register still uses TLS.
            "  assert(recomp_leaf_ld_eax() == 7);",  # Helpers bypass locals.
            "  recomp_leaf_st_eax(g_eax); recomp_leaf_st_esp(g_esp);",
            "  recomp_leaf_st_mm0(g_mm0);",
            "  g_fp_stack[g_fp_top] = 42.0; g_fp_top++;",
            "  assert(recomp_fp_top_ld() == 3);",
            "  recomp_fp_top_st(g_fp_top);",
            "}",
        ])
        source = r'''
#include <assert.h>
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#ifdef RECOMP_REGISTER_BLOCK
RECOMP_TLS RecompRegisterBlock g_recomp_registers;
#else
RECOMP_TLS uint32_t g_eax, g_esp;
RECOMP_TLS RecompMmx g_mm0;
RECOMP_TLS RecompXmm g_xmm2;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS int g_fp_top;
#endif
''' + "\n".join(body) + r'''
/* The aliases must be restored after the localized function. */
static void next(void) { g_eax += 5; g_mm0.q += 1; }
int main(void) {
    g_eax = 7; g_esp = 100; g_mm0.q = 1; g_fp_top = 3;
    localized(); next();
    assert(g_eax == 15 && g_esp == 104);
    assert(g_mm0.q == ((1 ^ 0x1234) + 1) && g_xmm2.q[1] == 99);
    assert(g_fp_stack[3] == 42.0 && g_fp_top == 4);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)
            (path / "probe.c").write_text(source)
            for flags in ([], ["-DRECOMP_REGISTER_BLOCK=1"]):
                with self.subTest(flags=flags):
                    result = subprocess.run([cc, "-std=c11", "-O2", "-I", str(runtime),
                                    *flags, str(path / "probe.c"), "-lm",
                                    "-o", str(path / "probe")],
                                   capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    subprocess.run([str(path / "probe")], check=True)


if __name__ == "__main__":
    unittest.main()
