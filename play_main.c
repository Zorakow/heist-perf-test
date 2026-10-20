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
static Layer *lay; static const float *lights; static const float *path;
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
static void skin_char(const Model *m, int f, float ox, float oy, float scx, float scy, vita2d_texture_vertex *out)
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
        px[i] = ox + x * scx; py[i] = oy + y * scy;
    }
    for (unsigned int i = 0; i < m->ni; i++) {
        unsigned int jj = i % 3, src = (scx < 0 && jj) ? i - jj + (jj == 1 ? 2 : 1) : i;   /* miroir: on inverse l'ordre des sommets */
        unsigned int k = m->idx[src]; out[i].x = px[k]; out[i].y = py[k]; out[i].z = 0.5f; out[i].u = m->vtx[k].u; out[i].v = m->vtx[k].v;
    }
}


/* ---------------- collision et entites (format GAM1) ---------------- */
typedef struct { unsigned char fl, h[17]; } TType;
typedef struct { unsigned char kind; float x, y; char name[24]; } Ent;
static float G_tw, G_th, G_ox, G_oy; static unsigned G_rows, G_cols;
static unsigned char *G_grid; static TType *G_types; static unsigned G_ntypes; static Ent *G_ents; static unsigned G_nent;
static int load_game(void)
{
    long n; unsigned char *b = slurp(DIR "heist_level_tutorial_game.bin", &n);
    if (!b || memcmp(b, "GAM1", 4)) return 0;
    memcpy(&G_tw, b + 4, 4); memcpy(&G_th, b + 8, 4); memcpy(&G_ox, b + 12, 4); memcpy(&G_oy, b + 16, 4);
    memcpy(&G_rows, b + 20, 4); memcpy(&G_cols, b + 24, 4);
    unsigned char *p = b + 28; G_grid = p; p += G_rows * G_cols;
    memcpy(&G_ntypes, p, 4); p += 4; G_types = (TType *)p; p += G_ntypes * 18;
    memcpy(&G_nent, p, 4); p += 4;
    G_ents = malloc(G_nent * sizeof(Ent));
    for (unsigned i = 0; i < G_nent; i++) { G_ents[i].kind = p[0]; memcpy(&G_ents[i].x, p + 4, 4); memcpy(&G_ents[i].y, p + 8, 4); memcpy(G_ents[i].name, p + 12, 24); p += 36; }
    return 1;
}
static float tile_top(const TType *t, float u)      /* hauteur locale (0..1) de la surface, <0 si aucune */
{
    float f = u * 16.0f; int i = (int)f; if (i < 0) i = 0; if (i > 15) i = 15; float fr = f - i;
    if (t->h[i] == 255 || t->h[i + 1] == 255) return -1.0f;
    return ((1.0f - fr) * t->h[i] + fr * t->h[i + 1]) / 254.0f;
}
/* Premiere surface sous (x, ytop) jusqu'a ymax. oneway_min: une plateforme n'est valable que si sa surface est >= cette valeur */
static int ground_at(float x, float ytop, float ymax, float oneway_min, float *out)
{
    int c = (int)floorf(x / G_tw - G_ox); if (c < 0 || c >= (int)G_cols) return 0;
    float u = x / G_tw - G_ox - c;
    int r0 = (int)floorf(ytop / G_th - G_oy), r1 = (int)floorf(ymax / G_th - G_oy);
    if (r0 < 0) r0 = 0;
    if (r1 >= (int)G_rows) r1 = G_rows - 1;
    for (int r = r0; r <= r1; r++) {
        unsigned char id = G_grid[r * G_cols + c]; if (!id) continue;
        const TType *t = &G_types[id]; if (!(t->fl & 3)) continue;
        float h = tile_top(t, u); if (h < 0) continue;
        float y = (G_oy + r + h) * G_th;
        if (y < ytop) continue;
        if ((t->fl & 2) && y < oneway_min) continue;
        *out = y; return 1;
    }
    return 0;
}
static int solid_at(float x, float y)               /* point dans une tuile pleine ? */
{
    int c = (int)floorf(x / G_tw - G_ox), r = (int)floorf(y / G_th - G_oy);
    if (c < 0 || c >= (int)G_cols) return 1;        /* les bords du niveau sont des murs */
    if (r < 0 || r >= (int)G_rows) return 0;
    unsigned char id = G_grid[r * G_cols + c]; if (!id) return 0;
    const TType *t = &G_types[id]; if (!(t->fl & 1)) return 0;
    float u = x / G_tw - G_ox - c, v = y / G_th - G_oy - r, h = tile_top(t, u);
    return h >= 0 && v >= h;
}

/* ---------------- joueur ---------------- */
typedef struct { float x, y, vx, vy; int ground, face; } Body;
#define GRAV 5200.0f
#define JUMP 1900.0f
#define STEP 90.0f
static void body_update(Body *b, float mv, float dt)
{
    float nx = b->x + mv * dt;
    if (mv != 0) b->face = mv > 0 ? 1 : -1;
    float side = (mv > 0 ? 70.0f : -70.0f);
    if (mv != 0 && (solid_at(nx + side, b->y - 30) || solid_at(nx + side, b->y - 200) || solid_at(nx + side, b->y - 380))) nx = b->x;
    b->x = nx;
    float gy;
    if (b->ground) {
        if (ground_at(b->x, b->y - STEP, b->y + 70.0f, b->y - 2.0f, &gy)) { b->y = gy; b->vy = 0; }
        else b->ground = 0;
    }
    if (!b->ground) {
        float oy = b->y; b->vy += GRAV * dt; if (b->vy > 4500) b->vy = 4500; b->y += b->vy * dt;
        if (b->vy < 0 && solid_at(b->x, b->y - 420)) b->vy = 0;
        if (b->vy >= 0 && ground_at(b->x, oy - 2.0f, b->y + 2.0f, oy - 2.0f, &gy) && gy <= b->y + 2.0f) { b->y = gy; b->vy = 0; b->ground = 1; }
    }
}

typedef struct { const Model *m; Body b; int mid; float t0; } Actor;
static int find_anim(const Model *m, const char *name) { for (unsigned a = 0; a < m->na; a++) if (!strcmp(m->anims[a].name, name)) return (int)a; return 0; }

int main(void)
{
    vita2d_init_advanced(6 * 1024 * 1024);
    vita2d_set_clear_color(RGBA8(14, 18, 28, 255));
    vita2d_pgf *pgf = vita2d_load_default_pgf();
    int ok = load_level() && load_game() && load_models() && nm >= 9;
    vita2d_texture *atl = ok ? load_atlas("heist_level_tutorial_atlas_quart.rgba") : NULL;
    vita2d_texture *catl = ok ? load_atlas("heist_multi_atlas_quarter.rgba") : NULL;
    if (!ok || !atl || !catl) {
        for (int i = 0; i < 500; i++) {
            vita2d_start_drawing(); vita2d_clear_screen();
            vita2d_pgf_draw_text(pgf, 30, 100, RGBA8(255, 80, 80, 255), 1.2f, "Fichiers manquants ou anciens dans ux0:data/heist/");
            vita2d_pgf_draw_text(pgf, 30, 140, RGBA8(255, 255, 255, 255), 1.0f, "heist_level_tutorial.bin, ..._game.bin, ..._atlas_quart.rgba, heist_multi.bin (9 modeles), heist_multi_atlas_quarter.rgba");
            vita2d_end_drawing(); vita2d_swap_buffers(); sceKernelDelayThread(16000);
        }
        sceKernelExitProcess(0);
    }
    enum { M_DAISY = 1, M_REVIVER = 7, M_GUARD = 6, M_SEABOT = 8 };
    Actor *act = calloc(G_nent + 1, sizeof(Actor)); unsigned na = 0;
    Actor P; memset(&P, 0, sizeof P); P.m = &M[M_DAISY]; P.mid = M_DAISY; P.b.face = 1; P.b.x = -1260; P.b.y = -600;
    for (unsigned i = 0; i < G_nent; i++) {
        const Ent *e = &G_ents[i];
        if (e->kind == 0) { P.b.x = e->x; P.b.y = e->y; continue; }
        int mid = e->kind == 3 ? M_GUARD : e->kind == 4 ? M_REVIVER : M_SEABOT;
        Actor *a = &act[na++]; a->mid = mid; a->m = &M[mid]; a->b.x = e->x; a->b.y = e->y; a->b.face = -1; a->t0 = (float)(i * 0.37);
        float gy; if (ground_at(e->x, e->y - 300, e->y + 3000, -1e9f, &gy)) a->b.y = gy;
        a->b.ground = 1;
    }
    float sx0 = P.b.x, sy0 = P.b.y; { float gy; if (ground_at(P.b.x, P.b.y - 300, P.b.y + 4000, -1e9f, &gy)) { P.b.y = gy; P.b.ground = 1; } sy0 = P.b.y; }
    Vis *vis = malloc(sizeof(Vis) * 24000); const unsigned VCAP = 24000; unsigned total_el = 0; for (unsigned i = 0; i < nlay; i++) total_el += lay[i].n;
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    SceCtrlData pad, old; memset(&old, 0, sizeof old);
    int mhz = 333, vsync = 0, hud = 1;
    scePowerSetArmClockFrequency(333); scePowerSetGpuClockFrequency(222); vita2d_set_vblank_wait(0);
    float camx = P.b.x, camy = P.b.y - 250; float fps = 0, ms = 0, cpu_ms = 0; int frames = 0; unsigned long long tlast = sceKernelGetProcessTimeWide(), cpu_acc = 0, tprev = tlast, T0 = tlast;
    unsigned nvis = 0; int p_idle = find_anim(P.m, "Idle"), p_walk = find_anim(P.m, "Walk"); (void)sx0;

    for (;;) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        unsigned int pr = pad.buttons & ~old.buttons; old = pad;
        if (pr & SCE_CTRL_START) break;
        if (pr & SCE_CTRL_SELECT) { vsync = !vsync; vita2d_set_vblank_wait(vsync); }
        if (pr & SCE_CTRL_LTRIGGER) hud = !hud;
        if (pr & SCE_CTRL_TRIANGLE) { mhz = (mhz == 333) ? 444 : 333; scePowerSetArmClockFrequency(mhz); }
        if ((pr & SCE_CTRL_CIRCLE) && fps > 0) {
            FILE *f = fopen(DIR "heist_play_test.txt", "a");
            if (f) { fprintf(f, "jeu=tutorial cpu=%dMHz vsync=%d x=%.0f y=%.0f elements=%u acteurs=%u -> %.1f fps (%.2f ms) CPU %.2f ms\n", mhz, vsync, P.b.x, P.b.y, nvis, na + 1, fps, ms, cpu_ms); fclose(f); }
        }
        unsigned long long tn = sceKernelGetProcessTimeWide(); float dt = (tn - tprev) / 1e6f; tprev = tn; if (dt > 0.05f) dt = 0.05f;
        float tsec = (tn - T0) / 1e6f;
        float ax = (pad.lx - 128) / 128.0f; if (fabsf(ax) < 0.2f) ax = 0;
        float dir = ax;
        if (pad.buttons & SCE_CTRL_LEFT) dir = -1;
        if (pad.buttons & SCE_CTRL_RIGHT) dir = 1;
        float speed = 1080.0f * ((pad.buttons & SCE_CTRL_RTRIGGER) ? 2.0f : 1.0f);
        if ((pr & SCE_CTRL_CROSS) && P.b.ground) { P.b.vy = -JUMP; P.b.ground = 0; }
        if (pr & SCE_CTRL_SQUARE) { P.b.x = sx0; P.b.y = sy0 - 400; P.b.vy = 0; P.b.ground = 0; }
        unsigned long long t0 = sceKernelGetProcessTimeWide();
        body_update(&P.b, dir * speed, dt);
        if (P.b.y > (G_oy + G_rows) * G_th + 600.0f) { P.b.x = sx0; P.b.y = sy0 - 400; P.b.vy = 0; }
        float k = dt * 6.0f; if (k > 1) k = 1; camx += (P.b.x - camx) * k; camy += (P.b.y - 260 - camy) * k;

        nvis = 0;
        for (unsigned li = 0; li < nlay; li++) {
            const Layer *L = &lay[li]; float f = PARA_D / (PARA_D + L->depth);
            float half = (SW * 0.5f) / (f * PPU), halfy = (SH * 0.5f) / (f * PPU);
            float xlo = camx - half, xhi = camx + half, ylo = camy - halfy, yhi = camy + halfy;
            for (unsigned i = 0; i < L->n; i++) {
                if (L->bnd[2 * i + 1] < xlo || L->bnd[2 * i] > xhi) continue;
                const float *e = L->el + 9 * i; float ext = e[2] + e[3];
                if (e[1] + ext < ylo || e[1] - ext > yhi) continue;
                if (nvis < VCAP) { vis[nvis].e = e; vis[nvis].f = f; nvis++; }
            }
        }
        vita2d_start_drawing(); vita2d_clear_screen();
        if (nvis) {
            vita2d_texture_vertex *buf = (vita2d_texture_vertex *)vita2d_pool_memalign(nvis * 6 * sizeof(vita2d_texture_vertex), 4);
            for (unsigned i = 0; i < nvis; i++) put_quad(buf + i * 6, vis[i].e, vis[i].f, camx, camy);
            unsigned tot = nvis * 6, off = 0;
            while (tot) { unsigned kk = tot > 65526 ? 65526 : tot; vita2d_draw_array_textured(atl, SCE_GXM_PRIMITIVE_TRIANGLES, buf + off, kk, RGBA8(255, 255, 255, 255)); off += kk; tot -= kk; }
        }
        /* acteurs visibles + joueur */
        const Actor *draw[64]; unsigned nd = 0; unsigned tot = 0;
        for (unsigned i = 0; i < na && nd < 62; i++) if (fabsf(act[i].b.x - camx) < SW * 0.5f / PPU + 500 && fabsf(act[i].b.y - camy) < SH * 0.5f / PPU + 700) { draw[nd++] = &act[i]; }
        draw[nd++] = &P;
        for (unsigned i = 0; i < nd; i++) tot += draw[i]->m->ni;
        vita2d_texture_vertex *cb = (vita2d_texture_vertex *)vita2d_pool_memalign(tot * sizeof(vita2d_texture_vertex), 4); size_t off = 0;
        for (unsigned i = 0; i < nd; i++) {
            const Actor *a = draw[i]; const Model *m = a->m; int moving = (a == &P) && fabsf(dir) > 0.01f && P.b.ground;
            int ai = (a == &P) ? (moving ? p_walk : p_idle) : find_anim(m, "Idle"); const Anim *A = &m->anims[ai];
            int fr = A->start + (int)((tsec + a->t0) * 30.0f) % A->count;
            float sx = (a->b.x - camx) * PPU + SW * 0.5f, sy = (a->b.y - camy) * PPU + SH * 0.5f; float sc = SKIN_U * PPU;
            skin_char(m, fr, sx, sy, sc * a->b.face, sc, cb + off); off += m->ni;
        }
        unsigned long long t1 = sceKernelGetProcessTimeWide(); cpu_acc += t1 - t0;
        { int total = tot, o2 = 0; while (total > 0) { int kk = total > 65532 ? 65532 : total; vita2d_draw_array_textured(catl, SCE_GXM_PRIMITIVE_TRIANGLES, cb + o2, kk, RGBA8(255, 255, 255, 255)); o2 += kk; total -= kk; } }
        if (hud) {
            vita2d_pgf_draw_textf(pgf, 10, 22, RGBA8(255, 255, 255, 255), 0.9f, "Tutorial: x %.0f  y %.0f  %s | %u elements | %u ennemis | CPU %d | vsync %s", P.b.x, P.b.y, P.b.ground ? "au sol" : "en l'air", nvis, na, mhz, vsync ? "ON" : "OFF");
            vita2d_pgf_draw_textf(pgf, 10, 46, RGBA8(255, 255, 0, 255), 1.2f, "%.1f fps  (%.2f ms)  CPU %.2f ms  [jouable v1]", fps, ms, cpu_ms);
            vita2d_pgf_draw_text(pgf, 10, 530, RGBA8(200, 200, 200, 255), 0.8f, "Stick/croix: marcher  Croix X: sauter  R: courir  Carre: retour au depart  Select: vsync  Triangle: 333/444  L: texte  Rond: noter  Start: quitter");
        }
        vita2d_end_drawing(); vita2d_wait_rendering_done(); vita2d_swap_buffers();
        unsigned long long te = sceKernelGetProcessTimeWide(); frames++;
        if (te - tlast >= 1000000ULL) { ms = (te - tlast) / 1000.0f / frames; fps = 1000.0f / ms; cpu_ms = cpu_acc / 1000.0f / frames; frames = 0; cpu_acc = 0; tlast = te; }
    }
    vita2d_free_texture(atl); vita2d_free_texture(catl); vita2d_free_pgf(pgf); vita2d_fini(); sceKernelExitProcess(0); return 0;
}
