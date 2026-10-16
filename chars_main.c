/* Test Vita : beaucoup de personnages animes (squelette + deformation des sommets).
   Compare : GROUPE (deformation CPU + 1 envoi GPU), PAR PIECE (1 dessin par piece), CPU SEUL.
   Aucun fichier du jeu : modeles et texture generes ici, a la taille des vrais personnages
   (profil A ~ Fabio : 22 pieces, 30 os ; profil B ~ gros personnage : 45 pieces). */
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
#define MAXB 32
#define MAXPARTS 48
#define MAXV 512
#define MAXT 512
#define MAXCH 48
#define TEXSZ 512

typedef struct { float a, b, c, d, tx, ty; } Xf;
typedef struct { float x, y, u, v; unsigned char bi[4]; float bw[4]; } Vtx;
typedef struct {
    int nb, np, nv, nt, ring;
    int parent[MAXB];
    Xf local[MAXB];           /* position de repos locale */
    Xf inv[MAXB];             /* inverse de la pose de repos */
    Vtx v[MAXV];
    unsigned short tri[MAXT * 3];
    int partBone[MAXPARTS];
    float partU[MAXPARTS], partV[MAXPARTS];
    float partX[MAXPARTS], partY[MAXPARTS];
} Model;

static Model models[2];
static const char *PROF_NAMES[2] = {"A (Fabio: 22 pieces)", "B (gros: 45 pieces)"};
static const int CH_COUNTS[] = {4, 8, 12, 16, 24, 32, 48};
#define N_CH 7
static const char *MODE_NAMES[3] = {"GROUPE", "PAR PIECE", "CPU SEUL"};

static vita2d_texture *atlas;

static void xf_mul(const Xf *p, const Xf *c, Xf *o)
{
    Xf r;
    r.a = p->a * c->a + p->b * c->c;  r.b = p->a * c->b + p->b * c->d;
    r.c = p->c * c->a + p->d * c->c;  r.d = p->c * c->b + p->d * c->d;
    r.tx = p->a * c->tx + p->b * c->ty + p->tx;
    r.ty = p->c * c->tx + p->d * c->ty + p->ty;
    *o = r;
}

static void xf_inv(const Xf *m, Xf *o)
{
    float det = m->a * m->d - m->b * m->c;
    float id = det != 0.0f ? 1.0f / det : 1.0f;
    Xf r;
    r.a = m->d * id;  r.b = -m->b * id;
    r.c = -m->c * id; r.d = m->a * id;
    r.tx = -(r.a * m->tx + r.b * m->ty);
    r.ty = -(r.c * m->tx + r.d * m->ty);
    *o = r;
}

static void make_model(Model *m, int nb, int np, int ring)
{
    memset(m, 0, sizeof(*m));
    m->nb = nb; m->np = np; m->ring = ring;
    Xf wr[MAXB];
    for (int i = 0; i < nb; i++) {
        m->parent[i] = i ? (i - 1) / 2 : -1;
        Xf l = {1, 0, 0, 1, (i % 3 - 1) * 9.0f, i ? -7.0f : 0.0f};
        m->local[i] = l;
        if (i) xf_mul(&wr[m->parent[i]], &l, &wr[i]); else wr[i] = l;
        xf_inv(&wr[i], &m->inv[i]);
    }
    int vi = 0, ti = 0;
    for (int p = 0; p < np; p++) {
        int b = p % nb;
        m->partBone[p] = b;
        m->partX[p] = wr[b].tx; m->partY[p] = wr[b].ty;
        m->partU[p] = (p % 8) * 64.0f; m->partV[p] = ((p / 8) % 8) * 64.0f;
        int c0 = vi;
        for (int k = 0; k <= ring; k++) {
            Vtx *v = &m->v[vi++];
            float ang = k == 0 ? 0 : (k - 1) * 6.2831853f / ring;
            float r = k == 0 ? 0 : 11.0f;
            v->x = wr[b].tx + cosf(ang) * r; v->y = wr[b].ty + sinf(ang) * r;
            v->u = m->partU[p] + 32 + cosf(ang) * r * 2.5f;
            v->v = m->partV[p] + 32 + sinf(ang) * r * 2.5f;
            int b1 = m->parent[b] >= 0 ? m->parent[b] : b;
            int b2 = (b + 1) % nb, b3 = (b + 2) % nb;
            v->bi[0] = b; v->bi[1] = b1; v->bi[2] = b2; v->bi[3] = b3;
            v->bw[0] = 0.62f; v->bw[1] = 0.24f; v->bw[2] = 0.10f; v->bw[3] = 0.04f;
        }
        for (int k = 1; k <= ring; k++) {
            m->tri[ti++] = c0; m->tri[ti++] = c0 + k; m->tri[ti++] = c0 + 1 + (k % ring);
        }
    }
    m->nv = vi; m->nt = ti / 3;
}

static void make_atlas(void)
{
    atlas = vita2d_create_empty_texture_format(TEXSZ, TEXSZ, SCE_GXM_TEXTURE_FORMAT_A8B8G8R8);
    unsigned int *p = (unsigned int *)vita2d_texture_get_datap(atlas);
    int stride = vita2d_texture_get_stride(atlas) / 4;
    for (int y = 0; y < TEXSZ; y++)
        for (int x = 0; x < TEXSZ; x++) {
            float dx = ((x % 64) - 32) / 32.0f, dy = ((y % 64) - 32) / 32.0f;
            float r2 = dx * dx + dy * dy;
            int a = r2 < 1.0f ? 255 : 0;
            int c = 90 + (((x / 64) * 37 + (y / 64) * 61) & 127);
            p[y * stride + x] = RGBA8(c, 255 - c / 2, 100 + (x / 64) * 12, a);
        }
    vita2d_texture_set_filters(atlas, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
}

/* Animation : chaque os tourne un peu, avec un decalage par personnage. */
static void pose(const Model *m, int ch, unsigned long long frame, Xf *W)
{
    float t = frame * 0.05f + ch * 0.7f;
    for (int i = 0; i < m->nb; i++) {
        float ang = sinf(t + i * 0.9f) * 0.35f;
        float cs = cosf(ang), sn = sinf(ang);
        Xf l = {cs, -sn, sn, cs, m->local[i].tx, m->local[i].ty};
        if (m->parent[i] >= 0) xf_mul(&W[m->parent[i]], &l, &W[i]); else W[i] = l;
    }
}

/* Matrices de deformation (monde * inverse repos) : 1 par os et par personnage. */
static void skin_char(const Model *m, const Xf *W, float ox, float oy, vita2d_texture_vertex *out)
{
    Xf S[MAXB];
    static float px[MAXV], py[MAXV];
    for (int i = 0; i < m->nb; i++) xf_mul(&W[i], &m->inv[i], &S[i]);
    for (int i = 0; i < m->nv; i++) {
        const Vtx *v = &m->v[i];
        float x = 0, y = 0;
        for (int k = 0; k < 4; k++) {
            const Xf *s = &S[v->bi[k]]; float w = v->bw[k];
            x += (s->a * v->x + s->b * v->y + s->tx) * w;
            y += (s->c * v->x + s->d * v->y + s->ty) * w;
        }
        px[i] = x + ox; py[i] = y + oy;
    }
    if (out) {
        for (int i = 0; i < m->nt * 3; i++) {
            int k = m->tri[i];
            out[i].x = px[k]; out[i].y = py[k]; out[i].z = 0.5f;
            out[i].u = m->v[k].u / TEXSZ; out[i].v = m->v[k].v / TEXSZ;
        }
    }
}

int main(void)
{
    vita2d_init_advanced(6 * 1024 * 1024);
    vita2d_set_clear_color(RGBA8(18, 22, 30, 255));
    vita2d_pgf *pgf = vita2d_load_default_pgf();
    make_atlas();
    make_model(&models[0], 30, 22, 6);
    make_model(&models[1], 32, 45, 9);

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
    SceCtrlData pad, old;
    memset(&old, 0, sizeof(old));
    int nc = 2, prof = 0, mode = 0, mhz = 333, vsync = 0, hud = 1;
    scePowerSetArmClockFrequency(333);
    scePowerSetGpuClockFrequency(222);
    vita2d_set_vblank_wait(0);

    float fps = 0, ms = 0, cpu_ms = 0;
    unsigned long long tlast = sceKernelGetProcessTimeWide(), cpu_acc = 0, frame = 0;
    int frames = 0;

    for (;;) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        unsigned int pr = pad.buttons & ~old.buttons;
        old = pad;
        int reset = 0;
        if (pr & SCE_CTRL_UP)    { if (nc < N_CH - 1) nc++; reset = 1; }
        if (pr & SCE_CTRL_DOWN)  { if (nc > 0) nc--; reset = 1; }
        if (pr & SCE_CTRL_CROSS) { prof = 1 - prof; reset = 1; }
        if (pr & SCE_CTRL_SQUARE){ mode = (mode + 1) % 3; reset = 1; }
        if (pr & SCE_CTRL_SELECT){ vsync = !vsync; vita2d_set_vblank_wait(vsync); reset = 1; }
        if (pr & SCE_CTRL_LTRIGGER) hud = !hud;
        if (pr & SCE_CTRL_TRIANGLE) { mhz = (mhz == 333) ? 444 : 333; scePowerSetArmClockFrequency(mhz); reset = 1; }
        if ((pr & SCE_CTRL_CIRCLE) && fps > 0) {
            FILE *f = fopen("ux0:data/heist_chars_test.txt", "a");
            if (f) {
                const Model *m = &models[prof];
                fprintf(f, "personnages=%d profil=%s mode=%s cpu=%dMHz vsync=%d sommets/perso=%d triangles/perso=%d -> %.1f fps (%.2f ms) deformation CPU %.2f ms\n",
                        CH_COUNTS[nc], PROF_NAMES[prof], MODE_NAMES[mode], mhz, vsync, m->nv, m->nt, fps, ms, cpu_ms);
                fclose(f);
            }
        }
        if (pr & SCE_CTRL_START) break;
        if (reset) { fps = 0; ms = 0; cpu_ms = 0; frames = 0; cpu_acc = 0; tlast = sceKernelGetProcessTimeWide(); }

        const Model *m = &models[prof];
        int n = CH_COUNTS[nc];
        unsigned long long t0 = sceKernelGetProcessTimeWide();

        vita2d_start_drawing();
        vita2d_clear_screen();

        vita2d_texture_vertex *buf = NULL;
        int tv = m->nt * 3;
        if (mode == 0) buf = (vita2d_texture_vertex *)vita2d_pool_memalign(n * tv * sizeof(vita2d_texture_vertex), 4);
        Xf W[MAXB];
        for (int c = 0; c < n; c++) {
            float ox = 70 + (c % 8) * 108.0f, oy = 110 + (c / 8) * 78.0f;
            pose(m, c, frame, W);
            if (mode == 1) {
                for (int p = 0; p < m->np; p++) {
                    const Xf *w = &W[m->partBone[p]];
                    float x = w->a * 0 + w->tx, y = w->c * 0 + w->ty;      /* centre de la piece */
                    float rot = atan2f(w->c, w->a);
                    vita2d_draw_texture_part_scale_rotate(atlas, ox + x, oy + y,
                        m->partU[p], m->partV[p], 64, 64, 0.35f, 0.35f, rot);
                }
            } else {
                skin_char(m, W, ox, oy, buf ? buf + c * tv : NULL);
            }
        }
        unsigned long long t1 = sceKernelGetProcessTimeWide();
        cpu_acc += t1 - t0;
        if (mode == 0 && buf) {
            int total = n * tv, off = 0;
            while (total > 0) {
                int k = total > 65532 ? 65532 : total;
                vita2d_draw_array_textured(atlas, SCE_GXM_PRIMITIVE_TRIANGLES, buf + off, k, RGBA8(255, 255, 255, 255));
                off += k; total -= k;
            }
        }
        if (hud) {
            vita2d_pgf_draw_textf(pgf, 10, 22, RGBA8(255, 255, 255, 255), 1.0f,
                "Personnages %d | profil %s | mode %s | %d sommets, %d triangles / perso | CPU %d | vsync %s",
                n, PROF_NAMES[prof], MODE_NAMES[mode], m->nv, m->nt, mhz, vsync ? "ON" : "OFF");
            vita2d_pgf_draw_textf(pgf, 10, 46, RGBA8(255, 255, 0, 255), 1.2f,
                "%.1f fps  (%.2f ms/image)  preparation CPU %.2f ms  [chars v1]", fps, ms, cpu_ms);
            vita2d_pgf_draw_text(pgf, 10, 520, RGBA8(200, 200, 200, 255), 0.9f,
                "Haut/Bas: nb persos  Croix: profil  Carre: mode  Select: vsync  Triangle: 333/444 MHz  L: cacher texte  Rond: noter  Start: quitter");
        }
        vita2d_end_drawing();
        vita2d_wait_rendering_done();
        vita2d_swap_buffers();

        unsigned long long te = sceKernelGetProcessTimeWide();
        frames++; frame++;
        if (te - tlast >= 1000000ULL) {
            ms = (te - tlast) / 1000.0f / frames;
            fps = 1000.0f / ms;
            cpu_ms = cpu_acc / 1000.0f / frames;
            frames = 0; cpu_acc = 0; tlast = te;
        }
    }
    vita2d_free_texture(atlas);
    vita2d_free_pgf(pgf);
    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}
