#ifndef NFSU_WIDESCREEN_H
#define NFSU_WIDESCREEN_H

typedef enum {
    NFSU_WIDESCREEN_NATIVE,
    NFSU_WIDESCREEN_LEGACY,
    NFSU_WIDESCREEN_ADAPTIVE
} NfsuWidescreenMode;

typedef struct {
    NfsuWidescreenMode mode;
    float aspect;
    float hud_scale;
    float edge_shift;
    float menu_expand;
} NfsuWidescreenLayout;

/* Startup default/rollback and per-frame policy. Native 4:3 needs no patch. */
void nfsu_widescreen_configure(const char *property);
NfsuWidescreenLayout nfsu_widescreen_layout(void);
void nfsu_widescreen_projection(const NfsuWidescreenLayout *layout,
                                int front_end, float *x, float *y);
float nfsu_widescreen_anchor(const NfsuWidescreenLayout *layout, float centre);
float nfsu_widescreen_garage_yaw(const NfsuWidescreenLayout *layout);

/* Guest adapters. Manual entry points retain their generated-body
 * declarations for the recompiler scanner, then delegate to these hooks. */
void nfsu_widescreen_view(void (*draw)(void));
void nfsu_widescreen_multiply(void (*draw)(void));
void nfsu_widescreen_minimap(void (*draw)(void));
void nfsu_widescreen_feng(void (*draw)(void));
void nfsu_widescreen_quad(void (*draw)(void));
void nfsu_widescreen_ui(void (*draw)(void));
int nfsu_widescreen_menu_begin(void);
void nfsu_widescreen_menu_end(int previous);

#endif
