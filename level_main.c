/* Test Vita : vrai niveau "tutorial" de SteamWorld Heist 2 (couches de decor en parallaxe, tuiles, lumieres)
   + jusqu'a 24 vrais personnages par dessus. Camera automatique le long du niveau.
   Donnees dans ux0:data/heist/ : heist_level_tutorial.bin + 3 atlas niveau,
   et pour les persos : heist_multi.bin + heist_multi_atlas_quarter.rgba. */
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
#define SW 960.0f
#define SH 544.0f
#define PPU 0.25f          /* pixels ecran par unite de monde (1:1 avec l'atlas au quart) */
#define PARA_D 1000.0f
#define SKIN_U 105.0f      /* unites de monde par unite de squelette */
#define MAXM 16

/* ---------------- niveau ---------------- */
typedef struct { float depth; unsigned n, flags; char name[24]; const float *bnd; const float *el; } Layer;
typedef struct { const float *e; float f; } Vis;
static unsigned char *lvbuf; static unsigned AW, AH, nlay, nlight, npath;
static Layer *lay; static const float *lights; /* 36 + 8 octets chacune */ static const float *path;
static const char *ATL_NAMES[3] = {"1/4 (14 Mo)", "1/6 (6,4 Mo)", "1/8 (3,6 Mo)"};
static const char *ATL_FILES[3] = {"heist_level_tutorial_atlas_quart.rgba", "heist_level_tutorial_atlas_sixieme.rgba", "heist_level_tutorial_atlas_huitieme.rgba"};

static void *slurp(const char *path_, long *len)
{
    FILE *f = fopen(path_, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    void *p = malloc(n);
    if (p && fread(p, 1, n, f) != (size_t)n) { free(p); p = NULL; }
    fclose(f); if (len) *len = n; return p;
}
static vita2d_texture *load_atlas(const char *file)
{
    char full[160]; snprintf(full, sizeof full, DIR "%s", file);
    FILE *f = fopen(full, "rb"); if (!f) return NULL;
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
static int load_level(void)
{
    long n; lvbuf = slurp(DIR "heist_level_tutorial.bin", &n);
    if (!lvbuf || memcmp(lvbuf, "LVL1", 4)) return 0;
    unsigned h[5]; memcpy(h, lvbuf + 4, 20); AW = h[0]; AH = h[1]; nlay = h[2]; nlight = h[3]; npath = h[4];
    lay = calloc(nlay, sizeof(Layer));
    unsigned char *p = lvbuf + 24;
    for (unsigned i = 0; i < nlay; i++) {
        Layer *L = &lay[i];
        memcpy(&L->depth, p, 4); memcpy(&L->n, p + 4, 4); memcpy(&L->flags, p + 8, 4); memcpy(L->name, p + 12, 24); p += 36;
        L->bnd = (const float *)p; p += 8 * L->n;
        L->el = (const float *)p; p += 36 * L->n;
    }
    lights = (const float *)p; p += 44 * nlight;
    path = (const float *)p;
    return 1;
}

static void put_quad(vita2d_texture_vertex *v, const float *e, float f, float camx, float camy)
{
    float sx = (e[0] - camx) * f * PPU + SW * 0.5f, sy = (e[1] - camy) * f * PPU + SH * 0.5f;
    float a = e[2] * f * PPU, b = e[3] * f * PPU, rot = e[4];
    float x[4], y[4]; const float dx[4] = {-1, 1, -1, 1}, dy[4] = {-1, -1, 1, 1};
    if (rot != 0.0f) {
        float c = cosf(rot), s = sinf(rot);
        for (int k = 0; k < 4; k++) { float px = dx[k] * a, py = dy[k] * b; x[k] = sx + c * px - s * py; y[k] = sy + s * px + c * py; }
    } else for (int k = 0; k < 4; k++) { x[k] = sx + dx[k] * a; y[k] = sy + dy[k] * b; }
    float u[4] = {e[5], e[7], e[5], e[7]}, w[4] = {e[6], e[6], e[8], e[8]};
    static const int ord[6] = {0, 1, 2, 1, 3, 2};
    for (int k = 0; k < 6; k++) { int q = ord[k]; v[k].x = x[q]; v[k].y = y[q]; v[k].z = 0.5f; v[k].u = u[q]; v[k].v = w[q]; }
}

static float path_y(float cx)
{
    const float *p = path;
    if (cx <= p[0]) return p[1];
    for (unsigned i = 1; i < npath; i++) if (cx <= p[2 * i]) { float t = (cx - p[2 * i - 2]) / (p[2 * i] - p[2 * i - 2]); return p[2 * i - 1] + t * (p[2 * i + 1] - p[2 * i - 1]); }
    return p[2 * npath - 1];
}

/* ---------------- personnages (meme format FAB2 que le test multi) ---------------- */
typedef struct { float a, b, c, d, tx, ty; } Xf;
typedef struct { float x, y, u, v; unsigned char bi[4]; unsigned char bw[4]; } Vtx;
typedef struct { char name[32]; unsigned int start, count; } Anim;
typedef struct { unsigned int nb, nv, ni, nf, na; int *parent; Xf *ibt; Vtx *vtx; unsigned short *idx; Xf *loc; Anim *anims; } Model;
static Model M[MAXM]; static unsigned int nm;
static int load_models(void)
{
    long n; unsigned char *b = (unsigned char *)slurp(DIR "heist_multi.bin", &n);
    if (!b || memcmp(b, "FAB2", 4)) return 0;
    unsigned int h[3]; memcpy(h, b + 4, 12); nm = h[0];
    unsigned char *p = b + 16;
    for (unsigned int i = 0; i < nm && i < MAXM; i++) {
        unsigned int len; memcpy(&len, p, 4); p += 4;
        unsigned char *q = p; Model *m = &M[i]; unsigned int hh[5]; memcpy(hh, q, 20); q += 20;
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
    Xf W[40], S[40]; static float px[1024], py[1024];
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
            x += (s->a * v->x + s->b * v->y + s->tx) * w; y += (s->c * v->x + s->d * v->y + s->ty) * w;
        }
        px[i] = ox + x * sc; py[i] = oy + y * sc;
    }
    for (unsigned int i = 0; i < m->ni; i++) { unsigned int k = m->idx[i]; out[i].x = px[k]; out[i].y = py[k]; out[i].z = 0.5f; out[i].u = m->vtx[k].u; out[i].v = m->vtx[k].v; }
}

int main(void)
{
    vita2d_init_advanced(6 * 1024 * 1024);
    vita2d_set_clear_color(RGBA8(14, 18, 28, 255));
    vita2d_pgf *pgf = vita2d_load_default_pgf();
    int ok = load_level();
    vita2d_texture *atl = ok ? load_atlas(ATL_FILES[0]) : NULL;
    int have_chars = load_models(); vita2d_texture *catl = have_chars ? load_atlas("heist_multi_atlas_quarter.rgba") : NULL;
    if (!catl) have_chars = 0;
    if (!ok || !atl) {
        for (int i = 0; i < 400; i++) {
            vita2d_start_drawing(); vita2d_clear_screen();
            vita2d_pgf_draw_text(pgf, 30, 100, RGBA8(255, 80, 80, 255), 1.2f, "Fichiers manquants dans ux0:data/heist/");
            vita2d_pgf_draw_text(pgf, 30, 140, RGBA8(255, 255, 255, 255), 1.0f, "heist_level_tutorial.bin + heist_level_tutorial_atlas_quart.rgba (+ sixieme, huitieme)");
            vita2d_end_drawing(); vita2d_swap_buffers(); sceKernelDelayThread(16000);
        }
        sceKernelExitProcess(0);
    }
    static const int COUNTS[] = {0, 4, 8, 12, 16, 24};
    static const float SPEEDS[] = {0, 500, 1500, 4000};
    static const char *WANT[2] = {"Idle", "Walk"};
    unsigned total_el = 0; for (unsigned i = 0; i < nlay; i++) total_el += lay[i].n;
    Vis *vis = malloc(sizeof(Vis) * 24000); const unsigned VCAP = 24000;
    unsigned maxni = 0; for (unsigned i = 0; i < nm; i++) if (M[i].ni > maxni) maxni = M[i].ni;
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
    SceCtrlData pad, old; memset(&old, 0, sizeof old);
    int nc = 3, sp = 1, mhz = 333, vsync = 0, hud = 1, at = 0, aw = 1, lightson = 0; if (!have_chars) nc = 0;
    scePowerSetArmClockFrequency(333); scePowerSetGpuClockFrequency(222); vita2d_set_vblank_wait(0);
    float x0 = -1500.f, x1 = path[2 * (npath - 1)]; float camx = 3000.f, dir = 1.f;
    float fps = 0, ms = 0, lv_ms = 0, ch_ms = 0, last_q = 0, last_t = 0; int last_l = 0;
    unsigned long long tlast = sceKernelGetProcessTimeWide(), lv_acc = 0, ch_acc = 0, tprev = tlast, frame = 0; int frames = 0;

    for (;;) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        unsigned int pr = pad.buttons & ~old.buttons; old = pad; int reset = 0;
        if ((pr & SCE_CTRL_UP) && have_chars) { if (nc < 5) nc++; reset = 1; }
        if ((pr & SCE_CTRL_DOWN) && have_chars) { if (nc > 0) nc--; reset = 1; }
        if (pr & SCE_CTRL_RIGHT) { if (sp < 3) sp++; }
        if (pr & SCE_CTRL_LEFT)  { if (sp > 0) sp--; }
        if (pr & SCE_CTRL_CROSS) { aw = !aw; reset = 1; }
        if (pr & SCE_CTRL_SQUARE) { lightson = !lightson; reset = 1; }
        if (pr & SCE_CTRL_RTRIGGER) {
            int nx = (at + 1) % 3; vita2d_texture *t = load_atlas(ATL_FILES[nx]);
            if (t) { vita2d_wait_rendering_done(); vita2d_free_texture(atl); atl = t; at = nx; } reset = 1;
        }
        if (pr & SCE_CTRL_SELECT) { vsync = !vsync; vita2d_set_vblank_wait(vsync); reset = 1; }
        if (pr & SCE_CTRL_LTRIGGER) hud = !hud;
        if (pr & SCE_CTRL_TRIANGLE) { mhz = (mhz == 333) ? 444 : 333; scePowerSetArmClockFrequency(mhz); reset = 1; }
        if ((pr & SCE_CTRL_CIRCLE) && fps > 0) {
            FILE *f = fopen(DIR "heist_level_test.txt", "a");
            if (f) {
                fprintf(f, "niveau=tutorial atlas=%s persos=%d anim=%s lumieres=%d cpu=%dMHz vsync=%d vitesse=%.0f quads=%.0f tri_persos=%.0f -> %.1f fps (%.2f ms) CPU niveau %.2f ms CPU persos %.2f ms\n",
                        ATL_NAMES[at], COUNTS[nc], WANT[aw], lightson, mhz, vsync, SPEEDS[sp], last_q, last_t, fps, ms, lv_ms, ch_ms);
                fclose(f);
            }
        }
        if (pr & SCE_CTRL_START) break;
        if (reset) { fps = ms = lv_ms = ch_ms = 0; frames = 0; lv_acc = ch_acc = 0; tlast = sceKernelGetProcessTimeWide(); }

        unsigned long long tn = sceKernelGetProcessTimeWide(); float dt = (tn - tprev) / 1e6f; tprev = tn; if (dt > 0.1f) dt = 0.1f;
        camx += dir * SPEEDS[sp] * dt;
        if (camx > x1) { camx = x1; dir = -1; } if (camx < x0) { camx = x0; dir = 1; }
        float camy = path_y(camx) - 120.0f;

        unsigned long long t0 = sceKernelGetProcessTimeWide();
        unsigned nv = 0;
        for (unsigned li = 0; li < nlay; li++) {
            const Layer *L = &lay[li]; float f = PARA_D / (PARA_D + L->depth);
            float half = (SW * 0.5f) / (f * PPU), halfy = (SH * 0.5f) / (f * PPU);
            float xlo = camx - half, xhi = camx + half, ylo = camy - halfy, yhi = camy + halfy;
            for (unsigned i = 0; i < L->n; i++) {
                if (L->bnd[2 * i + 1] < xlo || L->bnd[2 * i] > xhi) continue;
                const float *e = L->el + 9 * i; float ext = e[2] + e[3];
                if (e[1] + ext < ylo || e[1] - ext > yhi) continue;
                if (nv < VCAP) { vis[nv].e = e; vis[nv].f = f; nv++; }
            }
        }
        unsigned nl_vis = 0; const float *lq = NULL;
        vita2d_start_drawing(); vita2d_clear_screen();
        if (nv) {
            vita2d_texture_vertex *buf = (vita2d_texture_vertex *)vita2d_pool_memalign(nv * 6 * sizeof(vita2d_texture_vertex), 4);
            for (unsigned i = 0; i < nv; i++) put_quad(buf + i * 6, vis[i].e, vis[i].f, camx, camy);
            unsigned tot = nv * 6, off = 0;
            while (tot) { unsigned k = tot > 65526 ? 65526 : tot; vita2d_draw_array_textured(atl, SCE_GXM_PRIMITIVE_TRIANGLES, buf + off, k, RGBA8(255, 255, 255, 255)); off += k; tot -= k; }
        }
        if (lightson) { /* lumieres en texture: test de cout seulement (fondu normal, pas additif) */
            unsigned nlv = 0; static unsigned lidx[2048];
            for (unsigned i = 0; i < nlight && nlv < 2048; i++) {
                const float *e = (const float *)((const unsigned char *)lights + 44 * i);
                if (!(e[7] > e[5])) continue;
                if (fabsf(e[0] - camx) < SW * 0.5f / PPU + e[2] && fabsf(e[1] - camy) < SH * 0.5f / PPU + e[3]) lidx[nlv++] = i;
            }
            nl_vis = nlv;
            if (nlv) {
                vita2d_texture_vertex *lb = (vita2d_texture_vertex *)vita2d_pool_memalign(nlv * 6 * sizeof(vita2d_texture_vertex), 4);
                for (unsigned i = 0; i < nlv; i++) put_quad(lb + i * 6, (const float *)((const unsigned char *)lights + 44 * lidx[i]), 1.0f, camx, camy);
                vita2d_draw_array_textured(atl, SCE_GXM_PRIMITIVE_TRIANGLES, lb, nlv * 6, RGBA8(255, 255, 255, 70));
            }
        }
        (void)lq;
        unsigned long long t1 = sceKernelGetProcessTimeWide();
        float chtri = 0;
        if (COUNTS[nc] > 0) {
            int n = COUNTS[nc]; unsigned tot = 0; for (int c = 0; c < n; c++) tot += M[c % nm].ni;
            vita2d_texture_vertex *cb = (vita2d_texture_vertex *)vita2d_pool_memalign(tot * sizeof(vita2d_texture_vertex), 4);
            size_t off = 0;
            for (int c = 0; c < n; c++) {
                const Model *m = &M[c % nm]; const Anim *A = NULL;
                for (unsigned a = 0; a < m->na; a++) if (!strcmp(m->anims[a].name, WANT[aw])) A = &m->anims[a];
                if (!A) A = &m->anims[0];
                int fr = A->start + (int)((frame / 2 + c * 5) % A->count);
                float wx = camx + ((c % 8) - 3.5f) * 330.0f + (c / 8) * 120.0f, wy = path_y(wx) + 200.0f;
                float sx = (wx - camx) * PPU + SW * 0.5f, sy = (wy - camy) * PPU + SH * 0.5f;
                skin_char(m, fr, sx, sy, SKIN_U * PPU, cb + off); off += m->ni;
            }
            unsigned long long t2 = sceKernelGetProcessTimeWide(); ch_acc += t2 - t1;
            int total = tot, o2 = 0;
            while (total > 0) { int k = total > 65532 ? 65532 : total; vita2d_draw_array_textured(catl, SCE_GXM_PRIMITIVE_TRIANGLES, cb + o2, k, RGBA8(255, 255, 255, 255)); o2 += k; total -= k; }
            chtri = tot / 3.0f;
        }
        lv_acc += t1 - t0; last_q = (float)nv; last_t = chtri; last_l = nl_vis;
        if (hud) {
            vita2d_pgf_draw_textf(pgf, 10, 22, RGBA8(255, 255, 255, 255), 0.9f, "Niveau tutorial: %u elements visibles (sur %u) | %d persos | atlas %s | CPU %d | vsync %s | lumieres %s(%d)",
                nv, total_el, COUNTS[nc], ATL_NAMES[at], mhz, vsync ? "ON" : "OFF", lightson ? "ON" : "OFF", last_l);
            vita2d_pgf_draw_textf(pgf, 10, 46, RGBA8(255, 255, 0, 255), 1.2f, "%.1f fps  (%.2f ms)  CPU niveau %.2f ms  CPU persos %.2f ms  [level v1]", fps, ms, lv_ms, ch_ms);
            vita2d_pgf_draw_text(pgf, 10, 530, RGBA8(200, 200, 200, 255), 0.8f,
                "Haut/Bas: persos  Gauche/Droite: vitesse camera  Croix: anim  Carre: lumieres  R: atlas  Select: vsync  Triangle: 333/444  L: texte  Rond: noter  Start: quitter");
        }
        vita2d_end_drawing(); vita2d_wait_rendering_done(); vita2d_swap_buffers();
        unsigned long long te = sceKernelGetProcessTimeWide(); frames++; frame++;
        if (te - tlast >= 1000000ULL) { ms = (te - tlast) / 1000.0f / frames; fps = 1000.0f / ms; lv_ms = lv_acc / 1000.0f / frames; ch_ms = ch_acc / 1000.0f / frames; frames = 0; lv_acc = ch_acc = 0; tlast = te; }
    }
    vita2d_free_texture(atl); if (catl) vita2d_free_texture(catl);
    vita2d_free_pgf(pgf); vita2d_fini(); sceKernelExitProcess(0); return 0;
}
