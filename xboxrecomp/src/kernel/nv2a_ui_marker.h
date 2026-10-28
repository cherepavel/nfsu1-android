#ifndef NV2A_UI_MARKER_H
#define NV2A_UI_MARKER_H
#include <stdint.h>
#define NV2A_UI_BEGIN 0x4E554901u
#define NV2A_UI_END   0x4E554902u
#define NV2A_UI_PACKET 0x00040100u
/* Private port packets; ordinary guest NOPs retain their trap behavior. */
static inline int nv2a_ui_marker(uint32_t subch, uint32_t method, uint32_t param, int *begin)
{
    if (subch || method != 0x100 || (param != NV2A_UI_BEGIN && param != NV2A_UI_END))
        return 0;
    *begin = param == NV2A_UI_BEGIN;
    return 1;
}
#endif
