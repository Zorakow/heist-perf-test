/* Test de performance Vita : simule le remplissage d'un niveau 2D (sprites
   transparents superposes) + passes de lumiere plein ecran.
   Aucun fichier du jeu n'est utilise : les textures sont generees ici. */
#include <vita2d.h>
#include <psp2/ctrl.h>
#include <psp2/power.h>
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SW 960
#define SH 544
#define NTEX 6
#define TEXSZ 512
#define MAXSPR 512

typedef struct { float x, y, s, rot, par; int t; } Spr;

static vita2d_texture *tex[NTEX];
static vita2d_texture *lighttex;          /* degrade radial d'une lumiere */
static vita2d_texture *lm[2];             /* lightmaps 240x136 et 480x272 */
static const int LM_W[2] = {240, 480};
static const int LM_H[2] = {136, 272};
static const int LM_NAMES[3] = {0, 240, 480};
static const int LIGHT_COUNTS[] = {8, 16, 32};
static Spr spr[MAXSPR];
static const float OD_LEVELS[] = {1, 2, 3, 4, 6, 8, 12};
#define N_OD 7
static const int SPR_COUNTS[] = {64, 256};

static vita2d_texture *make_tex(int seed)
{
    vita2d_texture *t = vita2d_create_empty_texture_format(TEXSZ, TEXSZ, SCE_GXM_TEXTURE_FORMAT_A8B8G8R8);
    unsigned int *p = (unsigned int *)vita2d_texture_get_datap(t);
    int stride = vita2d_texture_get_stride(t) / 4;
    for (int y = 0; y < TEXSZ; y++) {
        for (int x = 0; x < TEXSZ; x++) {
            float dx = (x - TEXSZ / 2) / (float)(TEXSZ / 2);
            float dy = (y - TEXSZ / 2) / (float)(TEXSZ / 2);
            float r2 = dx * dx + dy * dy;
            int a = r2 < 1.0f ? (int)((1.0f - r2) * 255.0f) : 0;
            int c = 80 + ((x * 3 + y * 5 + seed * 40) & 127);
            p[y * stride + x] = RGBA8(c, 255 - c / 2, 120 + seed * 20, a);
        }
    }
    vita2d_texture_set_filters(t, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
    return t;
}

static vita2d_texture *make_light(void)
{
    const int N = 128;
    vita2d_texture *t = vita2d_create_empty_texture_format(N, N, SCE_GXM_TEXTURE_FORMAT_A8B8G8R8);
    unsigned int *p = (unsigned int *)vita2d_texture_get_datap(t);
    int stride = vita2d_texture_get_stride(t) / 4;
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            float dx = (x - N / 2) / (float)(N / 2), dy = (y - N / 2) / (float)(N / 2);
            float d = sqrtf(dx * dx + dy * dy);
            int a = d < 1.0f ? (int)((1.0f - d) * 255.0f) : 0;
            p[y * stride + x] = RGBA8(255, 255, 255, a);
        }
    vita2d_texture_set_filters(t, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
    return t;
}

/* Passe lightmap : on dessine les lumieres (additives) dans une petite texture,
   puis cette texture est etiree sur tout l'ecran. */
static void render_lightmap(int mode, int nlights, unsigned long long frame)
{
    int m = mode - 1;
    float k = LM_W[m] / (float)SW;              /* echelle ecran -> lightmap */
    vita2d_set_clear_color(RGBA8(40, 42, 60, 255));   /* lumiere ambiante */
    vita2d_start_drawing_advanced(lm[m], 0);
    vita2d_clear_screen();
    vita2d_set_blend_mode_add(1);
    for (int i = 0; i < nlights; i++) {
        float cx = fmodf(i * 97.0f + frame * (0.5f + (i % 5) * 0.2f), SW + 200) - 100;
        float cy = 60 + (i * 53) % (SH - 120);
        float r = 90 + (i % 4) * 40;            /* rayon en pixels ecran */
        float sc = (2 * r * k) / 128.0f;
        vita2d_draw_texture_tint_scale(lighttex, (cx - r) * k, (cy - r) * k, sc, sc,
                                       RGBA8(255, 190 + (i % 3) * 20, 120 + (i % 4) * 30, 255));
    }
    vita2d_set_blend_mode_add(0);
    vita2d_end_drawing();
    vita2d_set_clear_color(RGBA8(18, 22, 30, 255));
}

static void build_scene(int count, float overdraw)
{
    srand(1234);
    float area = overdraw * SW * SH / count;     /* surface moyenne d'un sprite */
    float s = sqrtf(area) / TEXSZ;                /* echelle d'un sprite carre */
    for (int i = 0; i < count; i++) {
        spr[i].s = s * (0.6f + 1.0f * (rand() % 100) / 100.0f);
        spr[i].x = (float)(rand() % SW);
        spr[i].y = (float)(rand() % SH);
        spr[i].rot = ((rand() % 100) / 100.0f - 0.5f) * 0.6f;
        spr[i].par = 0.2f + (rand() % 80) / 100.0f;
        spr[i].t = rand() % NTEX;
    }
}

int main(void)
{
    vita2d_init();
    vita2d_set_clear_color(RGBA8(18, 22, 30, 255));
    vita2d_pgf *pgf = vita2d_load_default_pgf();
    for (int i = 0; i < NTEX; i++) tex[i] = make_tex(i);
    lighttex = make_light();
    for (int i = 0; i < 2; i++) {
        lm[i] = vita2d_create_empty_texture_rendertarget(LM_W[i], LM_H[i], SCE_GXM_TEXTURE_FORMAT_A8B8G8R8);
        vita2d_texture_set_filters(lm[i], SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
    }

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
    SceCtrlData pad, old;
    memset(&old, 0, sizeof(old));

    int od = 1, sc = 0, passes = 0, vsync = 0, mhz = 333, lmode = 0, nl = 1;
    int logged = 0;
    scePowerSetArmClockFrequency(333);
    scePowerSetGpuClockFrequency(222);
    vita2d_set_vblank_wait(0);
    build_scene(SPR_COUNTS[sc], OD_LEVELS[od]);

    float fps = 0, ms = 0;
    unsigned long long t0 = sceKernelGetProcessTimeWide(), tlast = t0;
    int frames = 0;
    unsigned long long frame = 0;

    for (;;) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        unsigned int pr = pad.buttons & ~old.buttons;
        old = pad;
        int dirty = 0;
        if (pr & SCE_CTRL_UP)    { if (od < N_OD - 1) od++; dirty = 1; }
        if (pr & SCE_CTRL_DOWN)  { if (od > 0) od--; dirty = 1; }
        if (pr & SCE_CTRL_RIGHT) { if (passes < 3) passes++; }
        if (pr & SCE_CTRL_LEFT)  { if (passes > 0) passes--; }
        if (pr & SCE_CTRL_CROSS) { sc = 1 - sc; dirty = 1; }
        if (pr & SCE_CTRL_SELECT){ lmode = (lmode + 1) % 3; }
        if (pr & SCE_CTRL_RTRIGGER){ nl = (nl + 1) % 3; }
        if (pr & SCE_CTRL_SQUARE){ vsync = !vsync; vita2d_set_vblank_wait(vsync); }
        if (pr & SCE_CTRL_TRIANGLE) {
            mhz = (mhz == 333) ? 444 : 333;
            scePowerSetArmClockFrequency(mhz);
            scePowerSetGpuClockFrequency(mhz == 444 ? 222 : 222);
        }
        if (pr & SCE_CTRL_CIRCLE) {
            FILE *f = fopen("ux0:data/heist_perf_test.txt", "a");
            if (f) {
                fprintf(f, "overdraw=%.0f sprites=%d lumiere=%d lightmap=%d lumieres=%d cpu=%dMHz vsync=%d -> %.1f fps (%.2f ms)\n",
                        OD_LEVELS[od], SPR_COUNTS[sc], passes, LM_NAMES[lmode], LIGHT_COUNTS[nl], mhz, vsync, fps, ms);
                fclose(f);
                logged++;
            }
        }
        if (pr & SCE_CTRL_START) break;
        if (dirty) build_scene(SPR_COUNTS[sc], OD_LEVELS[od]);

        unsigned long long ts = sceKernelGetProcessTimeWide();
        if (lmode) render_lightmap(lmode, LIGHT_COUNTS[nl], frame);
        vita2d_start_drawing();
        vita2d_clear_screen();
        float cam = sinf(frame * 0.01f) * 120.0f;
        for (int i = 0; i < SPR_COUNTS[sc]; i++) {
            vita2d_draw_texture_scale_rotate(tex[spr[i].t], spr[i].x + cam * spr[i].par, spr[i].y,
                                             spr[i].s, spr[i].s, spr[i].rot);
        }
        if (lmode) {
            float k2 = SW / (float)LM_W[lmode - 1];
            vita2d_set_blend_mode_add(1);
            vita2d_draw_texture_scale(lm[lmode - 1], 0, 0, k2, k2);
            vita2d_set_blend_mode_add(0);
        }
        for (int k = 0; k < passes; k++)
            vita2d_draw_rectangle(0, 0, SW, SH, RGBA8(255, 200, 150, 40));
        vita2d_pgf_draw_textf(pgf, 10, 22, RGBA8(255, 255, 255, 255), 1.0f,
                              "Remplissage x%.0f | sprites %d | plein ecran %d | lightmap %d (%d lum.) | CPU %d | vsync %s",
                              OD_LEVELS[od], SPR_COUNTS[sc], passes, LM_NAMES[lmode], LIGHT_COUNTS[nl], mhz, vsync ? "ON" : "OFF");
        vita2d_pgf_draw_textf(pgf, 10, 46, RGBA8(255, 255, 0, 255), 1.2f, "%.1f fps  (%.2f ms/image)", fps, ms);
        vita2d_pgf_draw_text(pgf, 10, 520, RGBA8(200, 200, 200, 255), 0.9f,
                             "Haut/Bas: remplissage  Gauche/Droite: plein ecran  Select: lightmap  R: nb lumieres  Croix: nb sprites  Carre: vsync  Triangle: 333/444 MHz  Rond: noter  Start: quitter");
        vita2d_end_drawing();
        vita2d_wait_rendering_done();
        vita2d_swap_buffers();

        unsigned long long te = sceKernelGetProcessTimeWide();
        frames++;
        frame++;
        if (te - tlast >= 1000000ULL) {
            ms = (te - tlast) / 1000.0f / frames;
            fps = 1000.0f / ms;
            frames = 0;
            tlast = te;
        }
        (void)ts; (void)t0;
    }

    for (int i = 0; i < NTEX; i++) vita2d_free_texture(tex[i]);
    vita2d_free_texture(lighttex);
    for (int i = 0; i < 2; i++) vita2d_free_texture(lm[i]);
    vita2d_free_pgf(pgf);
    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}
