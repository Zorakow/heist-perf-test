/* Test Vita : scene de combat avec 8 VRAIS personnages de SteamWorld Heist 2 (equipage, marine, pirates, atomique),
   une seule texture partagee (3 tailles), deformation squelettique CPU, un seul envoi GPU.
   Donnees dans ux0:data/heist/ (heist_multi.bin + 3 atlas). */
#include <vita2d.h>
#include <psp2/ctrl.h>
#include <psp2/power.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define DIR "ux0:data/heist/"
#define MAXM 16
#define MAXCH 96

typedef struct { float x, y, u, v; unsigned char bi[4]; unsigned char bw[4]; } Vtx;
typedef struct { float a, b, c, d, tx, ty; } Xf;
typedef struct { char name[32]; unsigned int start, count; } Anim;
typedef struct {
    unsigned int nb, nv, ni, nf, na;
    int *parent; Xf *ibt; Vtx *vtx; unsigned short *idx; Xf *loc; Anim *anims;
} Model;

static Model M[MAXM]; static unsigned int nm, AW, AH;
static vita2d_texture *atlas[3];
static const char *ATL_NAMES[3] = {"plein (18 Mo)", "moitie (4,5 Mo)", "quart (1,1 Mo)"};
static const char *TAGS[8] = {"Fabio", "Daisy", "Diver", "Morgan", "Swab", "Berserker", "Guard", "Reviver"};

static void *slurp(const char *path, long *len)
{
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    void *p = malloc(n);
    if (p && fread(p, 1, n, f) != (size_t)n) { free(p); p = NULL; }
    fclose(f); if (len) *len = n; return p;
}

static vita2d_texture *load_atlas(const char *path)
{
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    unsigned int hd[2]; if (fread(hd, 4, 2, f) != 2) { fclose(f); return NULL; }
    vita2d_texture *t = vita2d_create_empty_texture_format(hd[0], hd[1], SCE_GXM_TEXTURE_FORMAT_A8B8G8R8);
    if (!t) { fclose(f); return NULL; }
    unsigned char *dst = (unsigned char *)vita2d_texture_get_datap(t);
    int stride = vita2d_texture_get_stride(t);
    unsigned char *row = malloc(hd[0] * 4);
    for (unsigned int y = 0; y < hd[1]; y++) { if (fread(row, 1, hd[0] * 4, f) != hd[0] * 4) break; memcpy(dst + y * stride, row, hd[0] * 4); }
    free(row); fclose(f);
    vita2d_texture_set_filters(t, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
    return t;
}

static int load_models(void)
{
    long n; unsigned char *b = (unsigned char *)slurp(DIR "heist_multi.bin", &n);
    if (!b || memcmp(b, "FAB2", 4)) return 0;
    unsigned int h[3]; memcpy(h, b + 4, 12); nm = h[0]; AW = h[1]; AH = h[2];
    unsigned char *p = b + 16;
    for (unsigned int i = 0; i < nm && i < MAXM; i++) {
        unsigned int len; memcpy(&len, p, 4); p += 4;
        unsigned char *q = p; Model *m = &M[i];
        unsigned int hh[5]; memcpy(hh, q, 20); q += 20;
        m->nb = hh[0]; m->nv = hh[1]; m->ni = hh[2]; m->nf = hh[3]; m->na = hh[4];
        m->parent = malloc(m->nb * 4); memcpy(m->parent, q, m->nb * 4); q += m->nb * 4;
        m->ibt = malloc(m->nb * 24); memcpy(m->ibt, q, m->nb * 24); q += m->nb * 24;
        m->vtx = malloc(m->nv * 24); memcpy(m->vtx, q, m->nv * 24); q += m->nv * 24;
        m->idx = malloc(m->ni * 2); memcpy(m->idx, q, m->ni * 2); q += m->ni * 2; if (m->ni & 1) q += 2;
        m->loc = malloc((size_t)m->nf * m->nb * 24); memcpy(m->loc, q, (size_t)m->nf * m->nb * 24); q += (size_t)m->nf * m->nb * 24;
        m->anims = malloc(m->na * sizeof(Anim)); memcpy(m->anims, q, m->na * sizeof(Anim));
        p += len;
    }
    free(b); return 1;
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

static void skin_char(const Model *m, int f, float ox, float oy, float sc, vita2d_texture_vertex *out)
{
    Xf W[40], S[40];
    static float px[1024], py[1024];
    const Xf *lf = m->loc + (size_t)f * m->nb;
    for (unsigned int i = 0; i < m->nb; i++) {
        if (m->parent[i] < 0) W[i] = lf[i]; else xf_mul(&W[m->parent[i]], &lf[i], &W[i]);
        const Xf *w = &W[i], *q = &m->ibt[i];
        S[i].a = w->a * q->a + w->b * q->c;  S[i].b = w->a * q->b + w->b * q->d;
        S[i].c = w->c * q->a + w->d * q->c;  S[i].d = w->c * q->b + w->d * q->d;
        S[i].tx = w->a * q->tx + w->b * q->ty + w->tx;
        S[i].ty = w->c * q->tx + w->d * q->ty + w->ty;
    }
    for (unsigned int i = 0; i < m->nv; i++) {
        const Vtx *v = &m->vtx[i]; float x = 0, y = 0;
        for (int k = 0; k < 4; k++) {
            if (!v->bw[k]) continue;
            const Xf *s = &S[v->bi[k]]; float w = v->bw[k] * (1.0f / 255.0f);
            x += (s->a * v->x + s->b * v->y + s->tx) * w;
            y += (s->c * v->x + s->d * v->y + s->ty) * w;
        }
        px[i] = ox + x * sc; py[i] = oy + y * sc;
    }
    for (unsigned int i = 0; i < m->ni; i++) {
        unsigned int k = m->idx[i];
        out[i].x = px[k]; out[i].y = py[k]; out[i].z = 0.5f; out[i].u = m->vtx[k].u; out[i].v = m->vtx[k].v;
    }
}

int main(void)
{
    vita2d_init_advanced(8 * 1024 * 1024);
    vita2d_set_clear_color(RGBA8(18, 22, 30, 255));
    vita2d_pgf *pgf = vita2d_load_default_pgf();
    int ok = load_models();
    atlas[0] = load_atlas(DIR "heist_multi_atlas_full.rgba");
    atlas[1] = load_atlas(DIR "heist_multi_atlas_half.rgba");
    atlas[2] = load_atlas(DIR "heist_multi_atlas_quarter.rgba");
    if (!ok || !atlas[0] || !atlas[1] || !atlas[2]) {
        for (int i = 0; i < 400; i++) {
            vita2d_start_drawing(); vita2d_clear_screen();
            vita2d_pgf_draw_text(pgf, 30, 100, RGBA8(255, 80, 80, 255), 1.2f, "Fichiers manquants ou illisibles dans ux0:data/heist/");
            vita2d_pgf_draw_text(pgf, 30, 140, RGBA8(255, 255, 255, 255), 1.0f, "heist_multi.bin + heist_multi_atlas_full/half/quarter.rgba");
            vita2d_end_drawing(); vita2d_swap_buffers(); sceKernelDelayThread(16000);
        }
        sceKernelExitProcess(0);
    }
    static const int COUNTS[] = {8, 16, 24, 32, 48, 64, 96};
    static const char *WANT[4] = {"Idle", "Walk", "MeleePunch", "CrouchIdle"};
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
    SceCtrlData pad, old; memset(&old, 0, sizeof(old));
    int nc = 1, mode = 0, mhz = 333, vsync = 0, hud = 1, at = 1, aw = 1;
    scePowerSetArmClockFrequency(333);
    scePowerSetGpuClockFrequency(222);
    vita2d_set_vblank_wait(0);
    static const char *MODES[3] = {"GROUPE", "CPU SEUL", "GPU SEUL"};
    unsigned int maxni = 0; for (unsigned int i = 0; i < nm; i++) if (M[i].ni > maxni) maxni = M[i].ni;
    vita2d_texture_vertex *fixedbuf = malloc((size_t)MAXCH * maxni * sizeof(vita2d_texture_vertex));
    int fixed_n = -1;
    float fps = 0, ms = 0, cpu_ms = 0; unsigned long long tlast = sceKernelGetProcessTimeWide(), cpu_acc = 0, frame = 0; int frames = 0;
    const float SC = 20.0f; unsigned int lasttri = 0;

    for (;;) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        unsigned int pr = pad.buttons & ~old.buttons; old = pad; int reset = 0;
        if (pr & SCE_CTRL_UP)    { if (nc < 6) nc++; reset = 1; }
        if (pr & SCE_CTRL_DOWN)  { if (nc > 0) nc--; reset = 1; }
        if (pr & SCE_CTRL_CROSS) { aw = (aw + 1) % 4; reset = 1; }
        if (pr & SCE_CTRL_SQUARE){ mode = (mode + 1) % 3; reset = 1; }
        if (pr & SCE_CTRL_RTRIGGER){ at = (at + 1) % 3; reset = 1; }
        if (pr & SCE_CTRL_SELECT){ vsync = !vsync; vita2d_set_vblank_wait(vsync); reset = 1; }
        if (pr & SCE_CTRL_LTRIGGER) hud = !hud;
        if (pr & SCE_CTRL_TRIANGLE) { mhz = (mhz == 333) ? 444 : 333; scePowerSetArmClockFrequency(mhz); reset = 1; }
        if ((pr & SCE_CTRL_CIRCLE) && fps > 0) {
            FILE *f = fopen("ux0:data/heist/heist_multi_test.txt", "a");
            if (f) {
                fprintf(f, "persos=%d mode=%s anim=%s atlas=%s cpu=%dMHz vsync=%d triangles_total=%u -> %.1f fps (%.2f ms) preparation CPU %.2f ms\n",
                        COUNTS[nc], MODES[mode], WANT[aw], ATL_NAMES[at], mhz, vsync, lasttri, fps, ms, cpu_ms);
                fclose(f);
            }
        }
        if (pr & SCE_CTRL_START) break;
        if (reset) { fps = ms = cpu_ms = 0; frames = 0; cpu_acc = 0; tlast = sceKernelGetProcessTimeWide(); fixed_n = -1; }

        int n = COUNTS[nc]; unsigned int tot = 0; for (int c = 0; c < n; c++) tot += M[c % nm].ni;
        lasttri = tot / 3;
        unsigned long long t0 = sceKernelGetProcessTimeWide();
        vita2d_start_drawing(); vita2d_clear_screen();
        vita2d_texture_vertex *buf;
        if (mode == 2) buf = fixedbuf; else buf = (mode == 0) ? (vita2d_texture_vertex *)vita2d_pool_memalign(tot * sizeof(vita2d_texture_vertex), 4) : fixedbuf;
        if (!(mode == 2 && fixed_n == n)) {
            size_t off = 0;
            for (int c = 0; c < n; c++) {
                const Model *m = &M[c % nm]; const Anim *A = NULL;
                for (unsigned int a = 0; a < m->na; a++) if (!strcmp(m->anims[a].name, WANT[aw])) A = &m->anims[a];
                if (!A) A = &m->anims[0];
                int f = A->start + (int)((frame / 2 + c * 5) % A->count);
                float ox = 60 + (c % 12) * 76.0f, oy = 130 + (c / 12) * 60.0f;
                skin_char(m, f, ox, oy, SC, buf + off); off += m->ni;
            }
            if (mode == 2) fixed_n = n;
        }
        unsigned long long t1 = sceKernelGetProcessTimeWide();
        cpu_acc += (mode == 2) ? 0 : (t1 - t0);
        if (mode != 1) {
            int total = tot, off = 0; vita2d_texture_vertex *src = buf;
            if (mode == 2) { src = (vita2d_texture_vertex *)vita2d_pool_memalign(total * sizeof(vita2d_texture_vertex), 4); memcpy(src, buf, total * sizeof(vita2d_texture_vertex)); }
            while (total > 0) { int k = total > 65532 ? 65532 : total; vita2d_draw_array_textured(atlas[at], SCE_GXM_PRIMITIVE_TRIANGLES, src + off, k, RGBA8(255, 255, 255, 255)); off += k; total -= k; }
        }
        if (hud) {
            vita2d_pgf_draw_textf(pgf, 10, 22, RGBA8(255, 255, 255, 255), 1.0f, "Scene x%d (%u triangles) | %s | anim %s | atlas %s | CPU %d | vsync %s",
                n, lasttri, MODES[mode], WANT[aw], ATL_NAMES[at], mhz, vsync ? "ON" : "OFF");
            vita2d_pgf_draw_textf(pgf, 10, 46, RGBA8(255, 255, 0, 255), 1.2f, "%.1f fps  (%.2f ms/image)  preparation CPU %.2f ms  [multi v1]", fps, ms, cpu_ms);
            vita2d_pgf_draw_text(pgf, 10, 520, RGBA8(200, 200, 200, 255), 0.9f,
                "Haut/Bas: nb persos  Croix: animation  Carre: mode  R: taille atlas  Select: vsync  Triangle: 333/444 MHz  L: cacher texte  Rond: noter  Start: quitter");
        }
        vita2d_end_drawing(); vita2d_wait_rendering_done(); vita2d_swap_buffers();
        unsigned long long te = sceKernelGetProcessTimeWide(); frames++; frame++;
        if (te - tlast >= 1000000ULL) { ms = (te - tlast) / 1000.0f / frames; fps = 1000.0f / ms; cpu_ms = cpu_acc / 1000.0f / frames; frames = 0; cpu_acc = 0; tlast = te; }
    }
    for (int i = 0; i < 3; i++) vita2d_free_texture(atlas[i]);
    vita2d_free_pgf(pgf); vita2d_fini(); sceKernelExitProcess(0); return 0;
}
