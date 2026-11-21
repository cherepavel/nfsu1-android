/* The same wrapper fixtures can exercise both supported register ABIs. */
#ifndef TEST_MANUAL_REGISTERS_H
#define TEST_MANUAL_REGISTERS_H
ptrdiff_t g_xbox_mem_offset;
#ifdef RECOMP_REGISTER_BLOCK
RECOMP_TLS RecompRegisterBlock g_recomp_registers;
#else
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
RECOMP_TLS uint32_t g_ebx, g_esi, g_edi, g_ebp, g_seh_ebp;
#endif
#endif
