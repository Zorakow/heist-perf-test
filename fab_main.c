/* Test Vita avec le VRAI Fabio de SteamWorld Heist 2 (donnees exportees par l'ingenieur, fichiers dans ux0:data/heist/).
   Squelette 32 os, 154 sommets, 96 triangles, 32 pieces regroupees en un seul envoi GPU, texture unique.
   Modes : GROUPE (deformation CPU + 1 envoi), CPU SEUL (deformation sans dessin), GPU SEUL (dessin d'un tampon deja calcule). */
#include <vita2d.h>
#include <psp2/ctrl.h>
#include <psp2/power.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SW 960
#define SH 544
#define MAXCH 64
#define DIR "ux0:data/heist/"

typedef struct { float x, y, u, v; unsigned char bi[4]; unsigned char bw[4]; } Vtx;
typedef struct { float a, b, c, d, tx, ty; } Xf;
typedef struct { char name[32]; unsigned int start, count; } Anim;

static unsigned int nb, nv, ni, npart, nf, AW, AH, nanim;
static int *parent;
static Xf *ibt, *loc;
static Vtx *vtx;
static unsigned short *idx;
static Anim *anims;
static vita2d_texture *atlas[2];
static const char *ATL_NAMES[2] = {"plein 1024x1024 (4 Mo)", "moitie 512x512 (1 Mo)"};

static void *slurp(const char *path, long *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    void *p = malloc(n);
    if (p && fread(p, 1, n, f) != (size_t)n) { free(p); p = NULL; }
    fclose(f);
    if (len) *len = n;
    return p;
}

static vita2d_texture *load_atlas(const char *path)
{
    long n; unsigned char *b = (unsigned char *)slurp(path, &n);
    if (!b) return NULL;
    unsigned int w, h; memcpy(&w, b, 4); memcpy(&h, b + 4, 4);
    vita2d_texture *t = vita2d_create_empty_texture_format(w, h, SCE_GXM_TEXTURE_FORMAT_A8B8G8R8);
    unsigned char *dst = (unsigned char *)vita2d_texture_get_datap(t);
    int stride = vita2d_texture_get_stride(t);
    for (unsigned int y = 0; y < h; y++) memcpy(dst + y * stride, b + 8 + y * w * 4, w * 4);
    vita2d_texture_set_filters(t, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
    free(b);
    return t;
}

static int load_model(void)
{
    long n; unsigned char *b = (unsigned char *)slurp(DIR "heist_fabio.bin", &n);
    if (!b || memcmp(b, "FAB1", 4)) return 0;
    unsigned int h[7]; memcpy(h, b + 4, 28);
    nb = h[0]; nv = h[1]; ni = h[2]; npart = h[3]; nf = h[4]; AW = h[5]; AH = h[6];
    unsigned char *p = b + 32;
    parent = malloc(nb * 4); memcpy(parent, p, nb * 4); p += nb * 4;
    ibt = malloc(nb * 24); memcpy(ibt, p, nb * 24); p += nb * 24;
    vtx = malloc(nv * 24); memcpy(vtx, p, nv * 24); p += nv * 24;
    idx = malloc(ni * 2); memcpy(idx, p, ni * 2); p += ni * 2;
    p += npart * 8;                                 /* liste des pieces : non utilisee, tout est dessine d'un coup */
    loc = malloc((size_t)nf * nb * 24); memcpy(loc, p, (size_t)nf * nb * 24); p += (size_t)nf * nb * 24;
    memcpy(&nanim, p, 4); p += 4;
    anims = malloc(nanim * sizeof(Anim)); memcpy(anims, p, nanim * sizeof(Anim));
    free(b);
    return 1;
}

static void xf_mul(const Xf *p, const Xf *c, Xf *o)
{
    Xf r;
    r.a = p->a * c->a + p->b * c->c;  r.b = p->a * c->b + p->b * c->d;
    r.c = p->c * c->a + p->d * c->c;  r.d = p->c * c->b + p->d * c->d;
    r.tx = p->a * c->tx + p->b * c->ty + p->tx;
    r.ty = p->c * c->tx + p->d * c->ty + p->ty;
    *o = r;
}

/* Deforme un personnage a l'image f et ecrit les triangles (ni sommets) dans out. */
static void skin_char(int f, float ox, float oy, float sc, vita2d_texture_vertex *out)
{
    Xf W[40], S[40];
    static float px[1024], py[1024];
    const Xf *lf = loc + (size_t)f * nb;
    for (unsigned int i = 0; i < nb; i++) {
        if (parent[i] < 0) W[i] = lf[i]; else xf_mul(&W[parent[i]], &lf[i], &W[i]);
        /* S = W * IBT (IBT : matrice 2x3 de repos inverse) */
        const Xf *m = &W[i], *q = &ibt[i];
        S[i].a = m->a * q->a + m->b * q->c;  S[i].b = m->a * q->b + m->b * q->d;
        S[i].c = m->c * q->a + m->d * q->c;  S[i].d = m->c * q->b + m->d * q->d;
        S[i].tx = m->a * q->tx + m->b * q->ty + m->tx;
        S[i].ty = m->c * q->tx + m->d * q->ty + m->ty;
    }
    for (unsigned int i = 0; i < nv; i++) {
        const Vtx *v = &vtx[i];
        float x = 0, y = 0;
        for (int k = 0; k < 4; k++) {
            if (!v->bw[k]) continue;
            const Xf *s = &S[v->bi[k]]; float w = v->bw[k] * (1.0f / 255.0f);
            x += (s->a * v->x + s->b * v->y + s->tx) * w;
            y += (s->c * v->x + s->d * v->y + s->ty) * w;
        }
        px[i] = ox + x * sc; py[i] = oy + y * sc;
    }
    for (unsigned int i = 0; i < ni; i++) {
        unsigned int k = idx[i];
        out[i].x = px[k]; out[i].y = py[k]; out[i].z = 0.5f; out[i].u = vtx[k].u; out[i].v = vtx[k].v;
    }
}

int main(void)
{
    vita2d_init_advanced(6 * 1024 * 1024);
    vita2d_set_clear_color(RGBA8(18, 22, 30, 255));
    vita2d_pgf *pgf = vita2d_load_default_pgf();
    int ok = load_model();
    atlas[0] = load_atlas(DIR "heist_fabio_atlas_full.rgba");
    atlas[1] = load_atlas(DIR "heist_fabio_atlas_half.rgba");
    if (!ok || !atlas[0] || !atlas[1]) {
        for (int i = 0; i < 300; i++) {
            vita2d_start_drawing(); vita2d_clear_screen();
            vita2d_pgf_draw_text(pgf, 30, 100, RGBA8(255, 80, 80, 255), 1.2f, "Fichiers manquants dans ux0:data/heist/");
            vita2d_pgf_draw_text(pgf, 30, 140, RGBA8(255, 255, 255, 255), 1.0f, "heist_fabio.bin, heist_fabio_atlas_full.rgba, heist_fabio_atlas_half.rgba");
            vita2d_end_drawing(); vita2d_swap_buffers(); sceKernelDelayThread(16000);
        }
        sceKernelExitProcess(0);
    }
    static const char *WANT[4] = {"Idle", "Walk", "MeleePunch", "CrouchIdle"};
    int ai[4], na = 0;
    for (int w = 0; w < 4; w++) for (unsigned int a = 0; a < nanim; a++) if (!strcmp(anims[a].name, WANT[w])) ai[na++] = a;

    static const int COUNTS[] = {4, 8, 16, 24, 32, 48, 64};
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
    SceCtrlData pad, old; memset(&old, 0, sizeof(old));
    int nc = 2, mode = 0, mhz = 333, vsync = 0, hud = 1, at = 0, an = 0;
    scePowerSetArmClockFrequency(333);
    scePowerSetGpuClockFrequency(222);
    vita2d_set_vblank_wait(0);
    static const char *MODES[3] = {"GROUPE", "CPU SEUL", "GPU SEUL"};
    vita2d_texture_vertex *fixedbuf = malloc(MAXCH * ni * sizeof(vita2d_texture_vertex));
    int fixed_n = -1;

    float fps = 0, ms = 0, cpu_ms = 0;
    unsigned long long tlast = sceKernelGetProcessTimeWide(), cpu_acc = 0, frame = 0;
    int frames = 0;
    const float SC = 22.0f;

    for (;;) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        unsigned int pr = pad.buttons & ~old.buttons; old = pad;
        int reset = 0;
        if (pr & SCE_CTRL_UP)    { if (nc < 6) nc++; reset = 1; }
        if (pr & SCE_CTRL_DOWN)  { if (nc > 0) nc--; reset = 1; }
        if (pr & SCE_CTRL_CROSS) { an = (an + 1) % na; reset = 1; }
        if (pr & SCE_CTRL_SQUARE){ mode = (mode + 1) % 3; reset = 1; }
        if (pr & SCE_CTRL_RTRIGGER){ at = 1 - at; reset = 1; }
        if (pr & SCE_CTRL_SELECT){ vsync = !vsync; vita2d_set_vblank_wait(vsync); reset = 1; }
        if (pr & SCE_CTRL_LTRIGGER) hud = !hud;
        if (pr & SCE_CTRL_TRIANGLE) { mhz = (mhz == 333) ? 444 : 333; scePowerSetArmClockFrequency(mhz); reset = 1; }
        if ((pr & SCE_CTRL_CIRCLE) && fps > 0) {
            FILE *f = fopen("ux0:data/heist/heist_fabio_test.txt", "a");
            if (f) {
                fprintf(f, "persos=%d mode=%s anim=%s atlas=%s cpu=%dMHz vsync=%d triangles/perso=%u -> %.1f fps (%.2f ms) preparation CPU %.2f ms\n",
                        COUNTS[nc], MODES[mode], anims[ai[an]].name, ATL_NAMES[at], mhz, vsync, ni / 3, fps, ms, cpu_ms);
                fclose(f);
            }
        }
        if (pr & SCE_CTRL_START) break;
        if (reset) { fps = ms = cpu_ms = 0; frames = 0; cpu_acc = 0; tlast = sceKernelGetProcessTimeWide(); fixed_n = -1; }

        int n = COUNTS[nc];
        const Anim *A = &anims[ai[an]];
        unsigned long long t0 = sceKernelGetProcessTimeWide();
        vita2d_start_drawing();
        vita2d_clear_screen();
        vita2d_texture_vertex *buf = NULL;
        if (mode == 2) {
            if (fixed_n != n) {
                for (int c = 0; c < n; c++) skin_char(A->start + (c * 3) % A->count, 60 + (c % 8) * 112.0f, 150 + (c / 8) * 62.0f, SC, fixedbuf + (size_t)c * ni);
                fixed_n = n;
            }
            buf = fixedbuf;
        } else {
            buf = (mode == 0) ? (vita2d_texture_vertex *)vita2d_pool_memalign(n * ni * sizeof(vita2d_texture_vertex), 4) : fixedbuf;
            for (int c = 0; c < n; c++) {
                int f = A->start + (int)((frame / 2 + c * 3) % A->count);
                skin_char(f, 60 + (c % 8) * 112.0f, 150 + (c / 8) * 62.0f, SC, buf + (size_t)c * ni);
            }
        }
        unsigned long long t1 = sceKernelGetProcessTimeWide();
        cpu_acc += (mode == 2) ? 0 : (t1 - t0);
        if (mode != 1) {
            int total = n * ni, off = 0;
            vita2d_texture_vertex *src = buf;
            if (mode == 2) { /* tampon CPU -> memoire GPU temporaire, copie comptee comme du CPU */
                vita2d_texture_vertex *g = (vita2d_texture_vertex *)vita2d_pool_memalign(total * sizeof(vita2d_texture_vertex), 4);
                memcpy(g, buf, total * sizeof(vita2d_texture_vertex)); src = g;
            }
            while (total > 0) {
                int k = total > 65532 ? 65532 : total;
                vita2d_draw_array_textured(atlas[at], SCE_GXM_PRIMITIVE_TRIANGLES, src + off, k, RGBA8(255, 255, 255, 255));
                off += k; total -= k;
            }
        }
        if (hud) {
            vita2d_pgf_draw_textf(pgf, 10, 22, RGBA8(255, 255, 255, 255), 1.0f,
                "Fabio x%d | %s | anim %s | atlas %s | CPU %d | vsync %s", n, MODES[mode], A->name, ATL_NAMES[at], mhz, vsync ? "ON" : "OFF");
            vita2d_pgf_draw_textf(pgf, 10, 46, RGBA8(255, 255, 0, 255), 1.2f,
                "%.1f fps  (%.2f ms/image)  preparation CPU %.2f ms  [fabio v1]", fps, ms, cpu_ms);
            vita2d_pgf_draw_text(pgf, 10, 520, RGBA8(200, 200, 200, 255), 0.9f,
                "Haut/Bas: nb persos  Croix: animation  Carre: mode  R: atlas plein/moitie  Select: vsync  Triangle: 333/444 MHz  L: cacher texte  Rond: noter  Start: quitter");
        }
        vita2d_end_drawing();
        vita2d_wait_rendering_done();
        vita2d_swap_buffers();
        unsigned long long te = sceKernelGetProcessTimeWide();
        frames++; frame++;
        if (te - tlast >= 1000000ULL) {
            ms = (te - tlast) / 1000.0f / frames; fps = 1000.0f / ms;
            cpu_ms = cpu_acc / 1000.0f / frames; frames = 0; cpu_acc = 0; tlast = te;
        }
    }
    vita2d_free_texture(atlas[0]); vita2d_free_texture(atlas[1]);
    vita2d_free_pgf(pgf); vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}
