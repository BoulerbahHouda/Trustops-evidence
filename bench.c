/*
 * TrustOps campaign v3 -- compiled benchmark.
 *
 * Inference (treelite-compiled forest), explanation (Saabas path
 * attribution), keyed input digest, record serialisation, four integrity
 * variants, four durability policies, anchoring, drift monitoring, tamper
 * tests and auditor verification, all in C and timed with
 * clock_gettime(CLOCK_MONOTONIC).
 *
 * Build: see run_campaign.sh (links the tl2cgen-generated model and
 * libcrypto). Usage: bench <build_dir> <trees> <out_dir> [--quick]
 */
#define _GNU_SOURCE
#define OPENSSL_SUPPRESS_DEPRECATED
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include "header.h" /* tl2cgen: predict(), get_num_feature() */

#define DIE(...) do { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); exit(1); } while (0)
#define K_TOP 3
#define MAXREC 65536
#define ANCHOR_EVERY 100

/* ------------------------------------------------------------------ io */

static void *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) DIE("cannot open %s", path);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *buf = malloc(n ? n : 1);
    if (fread(buf, 1, n, f) != (size_t)n) DIE("short read %s", path);
    fclose(f);
    if (len) *len = n;
    return buf;
}

static inline uint64_t now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec;
}

/* -------------------------------------------------------------- forest */

typedef struct {
    int n;
    int32_t *left, *right, *feat;
    double *thr, *p1;
} Tree;

typedef struct {
    int n_trees, d;
    Tree *t;
} Forest;

static Forest load_forest(const char *path) {
    size_t len;
    char *b = slurp(path, &len), *p = b;
    Forest F;
    memcpy(&F.n_trees, p, 4); p += 4;
    memcpy(&F.d, p, 4); p += 4;
    F.t = calloc(F.n_trees, sizeof(Tree));
    for (int i = 0; i < F.n_trees; i++) {
        Tree *t = &F.t[i];
        memcpy(&t->n, p, 4); p += 4;
        t->left = (int32_t *)p;  p += 4 * t->n;
        t->right = (int32_t *)p; p += 4 * t->n;
        t->feat = (int32_t *)p;  p += 4 * t->n;
        t->thr = (double *)p;    p += 8 * t->n;
        t->p1 = (double *)p;     p += 8 * t->n;
    }
    return F;
}

/* reference walk (used only for the parity check) */
static double predict_ref(const Forest *F, const double *x) {
    double s = 0;
    for (int i = 0; i < F->n_trees; i++) {
        const Tree *t = &F->t[i];
        int n = 0;
        while (t->left[n] != -1) n = x[t->feat[n]] <= t->thr[n] ? t->left[n] : t->right[n];
        s += t->p1[n];
    }
    return s / F->n_trees;
}

/* compiled inference: tl2cgen-generated predict() */
static union Entry *g_entry;
static double predict_tl(const double *x, int d) {
    for (int j = 0; j < d; j++) g_entry[j].fvalue = x[j];
    double r[2] = {0, 0};
    predict(g_entry, 0, r);
    return r[1];
}

/* Saabas path attribution: contributions of each feature along the
 * decision paths; root mean + sum(contrib) == forest output exactly. */
static void saabas(const Forest *F, const double *x, double *c) {
    memset(c, 0, sizeof(double) * F->d);
    for (int i = 0; i < F->n_trees; i++) {
        const Tree *t = &F->t[i];
        int n = 0;
        while (t->left[n] != -1) {
            int nx = x[t->feat[n]] <= t->thr[n] ? t->left[n] : t->right[n];
            c[t->feat[n]] += t->p1[nx] - t->p1[n];
            n = nx;
        }
    }
    for (int j = 0; j < F->d; j++) c[j] /= F->n_trees;
}

static void topk(const double *c, int d, int *idx, double *val) {
    for (int k = 0; k < K_TOP; k++) { idx[k] = -1; val[k] = 0; }
    for (int j = 0; j < d; j++) {
        double a = fabs(c[j]);
        for (int k = 0; k < K_TOP; k++) {
            if (idx[k] < 0 || a > fabs(val[k])) {
                for (int m = K_TOP - 1; m > k; m--) { idx[m] = idx[m - 1]; val[m] = val[m - 1]; }
                idx[k] = j; val[k] = c[j];
                break;
            }
        }
    }
}

/* ------------------------------------------------------------- hashing */

static void sha256(const void *m, size_t n, uint8_t out[32]) {
    SHA256_CTX c;   /* low-level API: avoids per-call EVP fetch overhead */
    SHA256_Init(&c); SHA256_Update(&c, m, n); SHA256_Final(out, &c);
}

/* HMAC-SHA256 with explicit pad handling so key material can be erased */
static void hmac_sha256(const uint8_t key[32], const void *m, size_t n, uint8_t out[32]) {
    uint8_t pad[64], inner[32];
    SHA256_CTX c;
    memset(pad, 0x36, 64);
    for (int i = 0; i < 32; i++) pad[i] ^= key[i];
    SHA256_Init(&c); SHA256_Update(&c, pad, 64); SHA256_Update(&c, m, n); SHA256_Final(inner, &c);
    memset(pad, 0x5c, 64);
    for (int i = 0; i < 32; i++) pad[i] ^= key[i];
    SHA256_Init(&c); SHA256_Update(&c, pad, 64); SHA256_Update(&c, inner, 32); SHA256_Final(out, &c);
    explicit_bzero(pad, 64);
    explicit_bzero(inner, 32);
    explicit_bzero(&c, sizeof c);
}

/* keyed input digest with a precomputed pad state (fixed per-node key) */
typedef struct { SHA256_CTX in, out; } HmacState;
static void hmac_prep(HmacState *s, const uint8_t key[32]) {
    uint8_t pad[64];
    memset(pad, 0x36, 64); for (int i = 0; i < 32; i++) pad[i] ^= key[i];
    SHA256_Init(&s->in); SHA256_Update(&s->in, pad, 64);
    memset(pad, 0x5c, 64); for (int i = 0; i < 32; i++) pad[i] ^= key[i];
    SHA256_Init(&s->out); SHA256_Update(&s->out, pad, 64);
    explicit_bzero(pad, 64);
}
static void hmac_fast(const HmacState *s, const void *m, size_t n, uint8_t out[32]) {
    SHA256_CTX c = s->in;
    uint8_t inner[32];
    SHA256_Update(&c, m, n); SHA256_Final(inner, &c);
    c = s->out;
    SHA256_Update(&c, inner, 32); SHA256_Final(out, &c);
}

static void evolve_key(uint8_t key[32]) {
    uint8_t buf[38], nk[32];
    memcpy(buf, "evolve", 6);
    memcpy(buf + 6, key, 32);
    sha256(buf, 38, nk);
    explicit_bzero(buf, 38);
    explicit_bzero(key, 32);      /* old key erased */
    memcpy(key, nk, 32);
    explicit_bzero(nk, 32);
}

/* -------------------------------------------------------------- ed25519 */

static EVP_PKEY *g_sk;
static size_t ed_sign(const uint8_t *m, size_t n, uint8_t sig[64]) {
    EVP_MD_CTX *c = EVP_MD_CTX_new();
    size_t sl = 64;
    if (EVP_DigestSignInit(c, NULL, NULL, NULL, g_sk) != 1 ||
        EVP_DigestSign(c, sig, &sl, m, n) != 1) DIE("ed25519 sign");
    EVP_MD_CTX_free(c);
    return sl;
}
static int ed_verify(const uint8_t *m, size_t n, const uint8_t sig[64]) {
    EVP_MD_CTX *c = EVP_MD_CTX_new();
    int ok = EVP_DigestVerifyInit(c, NULL, NULL, NULL, g_sk) == 1 &&
             EVP_DigestVerify(c, sig, 64, m, n) == 1;
    EVP_MD_CTX_free(c);
    return ok;
}

/* -------------------------------------------------------------- record */

enum { L1 = 1, L2 = 2, L3 = 3 };
static const char *MODEL_VERSION = "rf-unsw15-v3";
static const char *ROLE = "net-ops.ids.oncall";

typedef struct {
    uint64_t seq, ts;
    uint8_t digest[32], prev[32];
    int decision;
    double conf;
    int drift, response;   /* 0 ok/none, 1 warn, 2 drift/throttle */
    int level, d;
    int tk_idx[K_TOP];
    double tk_val[K_TOP];
    const double *x;
    char model_version[32];
} Rec;

static const char HEX[] = "0123456789abcdef";
static char *hexcat(char *p, const uint8_t *h) {
    for (int i = 0; i < 32; i++) { *p++ = HEX[h[i] >> 4]; *p++ = HEX[h[i] & 15]; }
    return p;
}
static const char *DRIFT_S[] = {"ok", "warn", "drift"};
static const char *RESP_S[] = {"none", "none", "throttle"};

/* JSON, hash fields hex-encoded; deterministic field order */
static size_t ser_json(const Rec *r, char *o) {
    char *p = o;
    p += sprintf(p, "{\"seq\":%llu,\"ts\":%llu,\"model_version\":\"%s\",\"input_digest\":\"",
                 (unsigned long long)r->seq, (unsigned long long)r->ts, r->model_version);
    p = hexcat(p, r->digest);
    p += sprintf(p, "\",\"decision\":%d,\"confidence\":%.4f,\"drift_state\":\"%s\",\"response\":\"%s\","
                    "\"responsible_role\":\"%s\",\"prev_hash\":\"",
                 r->decision, r->conf, DRIFT_S[r->drift], RESP_S[r->response], ROLE);
    p = hexcat(p, r->prev);
    *p++ = '"';
    if (r->level >= L2) {
        p += sprintf(p, ",\"explanation\":[");
        for (int k = 0; k < K_TOP; k++)
            p += sprintf(p, "%s[%d,%.4f]", k ? "," : "", r->tk_idx[k], r->tk_val[k]);
        *p++ = ']';
    }
    if (r->level == L3) {
        p += sprintf(p, ",\"features\":[");
        for (int j = 0; j < r->d; j++) p += sprintf(p, "%s%.6g", j ? "," : "", r->x[j]);
        *p++ = ']';
    }
    *p++ = '}';
    *p = 0;
    return p - o;
}

/* compact binary encoding, raw 32-byte hashes, float64 features */
static size_t ser_bin(const Rec *r, uint8_t *o) {
    uint8_t *p = o;
#define PUT(v) do { memcpy(p, &(v), sizeof(v)); p += sizeof(v); } while (0)
    uint8_t mvl = (uint8_t)strlen(r->model_version), rl = (uint8_t)strlen(ROLE);
    PUT(r->seq); PUT(r->ts);
    *p++ = mvl; memcpy(p, r->model_version, mvl); p += mvl;
    memcpy(p, r->digest, 32); p += 32;
    *p++ = (uint8_t)r->decision;
    float cf = (float)r->conf; PUT(cf);
    *p++ = (uint8_t)r->drift; *p++ = (uint8_t)r->response;
    *p++ = rl; memcpy(p, ROLE, rl); p += rl;
    memcpy(p, r->prev, 32); p += 32;
    if (r->level >= L2)
        for (int k = 0; k < K_TOP; k++) {
            uint16_t i = (uint16_t)r->tk_idx[k]; float v = (float)r->tk_val[k];
            PUT(i); PUT(v);
        }
    if (r->level == L3) { memcpy(p, r->x, 8 * r->d); p += 8 * r->d; }
#undef PUT
    return p - o;
}

/* --------------------------------------------------------- integrity */

enum { I_CHAIN, I_FSHMAC, I_FSSAGG, I_ED25519, N_INTEG };
static const char *INTEG_S[] = {"sha256-chain", "fs-hmac", "fssagg-mac", "ed25519-record"};

typedef struct {
    int kind;
    uint8_t head[32], key[32], agg[32];
} Integ;

static void integ_init(Integ *g, int kind, const uint8_t k0[32]) {
    memset(g, 0, sizeof *g);
    g->kind = kind;
    memcpy(g->key, k0, 32);
}

/* returns tag length written to tag (0, 32 or 64) */
static size_t integ_link(Integ *g, const uint8_t *blob, size_t n, uint8_t *tag) {
    switch (g->kind) {
    case I_CHAIN:
        sha256(blob, n, g->head);
        return 0;
    case I_FSHMAC: {
        hmac_sha256(g->key, blob, n, tag);
        evolve_key(g->key);
        uint8_t buf[32 + 32];
        sha256(blob, n, buf);
        memcpy(buf + 32, tag, 32);
        sha256(buf, 64, g->head);
        return 32;
    }
    case I_FSSAGG: {      /* Ma-Tsudik FssAgg-MAC: only the aggregate is kept */
        uint8_t t[32], buf[64];
        hmac_sha256(g->key, blob, n, t);
        evolve_key(g->key);
        memcpy(buf, g->agg, 32); memcpy(buf + 32, t, 32);
        sha256(buf, 64, g->agg);
        explicit_bzero(t, 32);
        sha256(blob, n, g->head);
        return 0;
    }
    case I_ED25519:
        sha256(blob, n, g->head);
        return ed_sign(g->head, 32, tag);
    }
    return 0;
}

/* --------------------------------------------------------- durability */

enum { W_BUFFERED, W_WRITE, W_FSYNC100, W_FSYNC10, W_FSYNC1, N_WPOL };
static const char *WPOL_S[] = {"buffered", "write", "fsync-every-100", "fsync-every-10", "fsync-every-1"};

typedef struct {
    int pol, fd;
    FILE *f;
    uint64_t n;
} Log;

static void log_open(Log *L, const char *path, int pol) {
    memset(L, 0, sizeof *L);
    L->pol = pol;
    unlink(path);
    if (pol == W_BUFFERED) L->f = fopen(path, "wb");
    else L->fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
}
static void log_append(Log *L, const void *b, size_t n, const uint8_t *tag, size_t tl) {
    uint8_t fr[8192];
    uint32_t len = (uint32_t)n;
    memcpy(fr, &len, 4); memcpy(fr + 4, b, n); if (tl) memcpy(fr + 4 + n, tag, tl);
    size_t m = 4 + n + tl;
    if (L->pol == W_BUFFERED) { fwrite(fr, 1, m, L->f); return; }
    if (write(L->fd, fr, m) != (ssize_t)m) DIE("write");
    L->n++;
    if (L->pol == W_FSYNC1 || (L->pol == W_FSYNC10 && L->n % 10 == 0) ||
        (L->pol == W_FSYNC100 && L->n % 100 == 0)) fsync(L->fd);
}
static void log_close(Log *L) {
    if (L->f) fclose(L->f);
    else { fsync(L->fd); close(L->fd); }
}

/* ----------------------------------------------------------- anchoring */

/* An anchor binds node id, record count, chain head and (if any) the
 * FssAgg aggregate, signed with the node's Ed25519 key, and is sent to
 * the orchestrator over a stream socket. */
typedef struct {
    char node[16];
    uint64_t count, ts;
    uint8_t head[32], agg[32];
    uint8_t sig[64];
} Anchor;

static int g_anchor_fd = -1;
static void *drain(void *arg) {
    int fd = *(int *)arg;
    char b[4096];
    while (read(fd, b, sizeof b) > 0) {}
    return NULL;
}
static void anchor_emit(const Integ *g, uint64_t count, Anchor *a) {
    memset(a, 0, sizeof *a);
    strcpy(a->node, "node-01");
    a->count = count;
    a->ts = now_ns();
    memcpy(a->head, g->head, 32);
    memcpy(a->agg, g->agg, 32);
    ed_sign((const uint8_t *)a, offsetof(Anchor, sig), a->sig);
    if (g_anchor_fd >= 0 && write(g_anchor_fd, a, sizeof *a) != sizeof *a) DIE("anchor write");
}

/* ------------------------------------------------------------- drift */

typedef struct {
    int d;
    double *mu, *sd, *ew, alpha, warn, drift;
} Drift;

static void drift_init(Drift *m, int d, const double *mu, const double *sd, double warn, double drift) {
    m->d = d; m->mu = (double *)mu; m->alpha = 0.02; m->warn = warn; m->drift = drift;
    m->sd = malloc(8 * d); m->ew = malloc(8 * d);
    for (int j = 0; j < d; j++) { m->sd[j] = sd[j] > 0 ? sd[j] : 1; m->ew[j] = mu[j]; }
}
static int drift_update(Drift *m, const double *x) {
    double s = 0;
    for (int j = 0; j < m->d; j++) {
        m->ew[j] += m->alpha * (x[j] - m->ew[j]);
        double z = fabs(m->ew[j] - m->mu[j]) / m->sd[j];
        if (z > s) s = z;
    }
    return s >= m->drift ? 2 : s >= m->warn ? 1 : 0;
}

/* ------------------------------------------------------------- globals */

static Forest F;
static int D, NROWS;
static double *X, *REF, *TSTAT;
static HmacState g_dig;
static uint8_t g_k0[32];
static int g_quick;
static char g_out[512];

/* append logs live in the container filesystem (overlay on the VM disk),
 * not in the bind-mounted results folder */
static const char *logdir(void) { const char *d = getenv("TRUSTOPS_LOGDIR"); return d ? d : "/tmp"; }

static FILE *openw(const char *name) {
    char p[700];
    snprintf(p, sizeof p, "%s/%s", g_out, name);
    FILE *f = fopen(p, "w");
    if (!f) DIE("cannot write %s", p);
    return f;
}

/* ========================================================= E1: ops */

/* Batched timing: each sample times B consecutive operations, cycling
 * through stream rows, and records ns/op. R samples per operation. */
typedef void (*OpFn)(int row, void *ctx);
static void time_op(FILE *o, const char *op, const char *level, OpFn fn, void *ctx, int R) {
    int B = 1;
    for (;;) {                        /* calibrate: batch >= 50 us */
        uint64_t t0 = now_ns();
        for (int i = 0; i < B; i++) fn(i % NROWS, ctx);
        if (now_ns() - t0 >= 50000 || B >= (1 << 20)) break;
        B *= 2;
    }
    for (int w = 0; w < 3; w++) for (int i = 0; i < B; i++) fn(i % NROWS, ctx);
    int row = 0;
    for (int r = 0; r < R; r++) {
        uint64_t t0 = now_ns();
        for (int i = 0; i < B; i++) { fn(row, ctx); row = (row + 1) % NROWS; }
        double ns = (double)(now_ns() - t0) / B;
        fprintf(o, "%s,%s,%d,%d,%.2f\n", op, level, r, B, ns);
    }
}

static volatile double g_sink;
static uint8_t g_hb[32];
static void op_infer(int i, void *c) { (void)c; g_sink += predict_tl(X + (size_t)i * D, D); }
static void op_infer_ref(int i, void *c) { (void)c; g_sink += predict_ref(&F, X + (size_t)i * D); }
static void op_saabas(int i, void *c) {
    double *cb = c; int idx[K_TOP]; double v[K_TOP];
    saabas(&F, X + (size_t)i * D, cb); topk(cb, D, idx, v); g_sink += v[0];
}
static void op_digest_plain(int i, void *c) { (void)c; sha256(X + (size_t)i * D, 8 * D, g_hb); }
static void op_digest_keyed(int i, void *c) { (void)c; hmac_fast(&g_dig, X + (size_t)i * D, 8 * D, g_hb); }
static Drift g_drift;
static void op_drift(int i, void *c) { (void)c; g_sink += drift_update(&g_drift, X + (size_t)i * D); }

typedef struct { Rec r; char buf[16384]; int bin; } SerCtx;
static void op_serialize(int i, void *c) {
    SerCtx *s = c;
    s->r.x = X + (size_t)i * D; s->r.seq = i;
    g_sink += s->bin ? ser_bin(&s->r, (uint8_t *)s->buf) : ser_json(&s->r, s->buf);
}
typedef struct { Integ g; uint8_t blob[600]; size_t n; uint8_t tag[64]; } IntCtx;
static void op_integ(int i, void *c) { IntCtx *s = c; s->blob[0] = (uint8_t)i; integ_link(&s->g, s->blob, s->n, s->tag); }
static void op_anchor(int i, void *c) { Anchor a; anchor_emit((Integ *)c, i, &a); }
typedef struct { Log L; uint8_t blob[600]; size_t n; } LogCtx;
static void op_append(int i, void *c) { LogCtx *s = c; (void)i; log_append(&s->L, s->blob, s->n, NULL, 0); }

static void fill_rec(Rec *r, int level, int row) {
    memset(r, 0, sizeof *r);
    r->level = level; r->d = D; r->x = X + (size_t)row * D;
    r->ts = 1790000000000000000ull; r->decision = 1; r->conf = 0.8731;
    strcpy(r->model_version, MODEL_VERSION);
    RAND_bytes(r->digest, 32); RAND_bytes(r->prev, 32);
    double cb[1024]; saabas(&F, r->x, cb); topk(cb, D, r->tk_idx, r->tk_val);
}

static void e1_ops(int R) {
    FILE *o = openw("e1_ops_raw.csv");
    fprintf(o, "op,level,sample,batch,ns_per_op\n");
    double *cb = malloc(8 * D);
    time_op(o, "inference_compiled", "-", op_infer, NULL, R);
    time_op(o, "inference_reference_walk", "-", op_infer_ref, NULL, R);
    time_op(o, "explain_saabas_top3", "-", op_saabas, cb, R);
    time_op(o, "digest_sha256", "-", op_digest_plain, NULL, R);
    time_op(o, "digest_keyed_hmac", "-", op_digest_keyed, NULL, R);
    time_op(o, "drift_update", "-", op_drift, NULL, R);
    static SerCtx s;
    const char *lv[] = {"", "L1", "L2", "L3"};
    for (int L = L1; L <= L3; L++) {
        fill_rec(&s.r, L, 0);
        s.bin = 0; time_op(o, "serialize_json", lv[L], op_serialize, &s, R);
        s.bin = 1; time_op(o, "serialize_binary", lv[L], op_serialize, &s, R);
    }
    /* integrity on a representative L2 JSON record */
    fill_rec(&s.r, L2, 0);
    static IntCtx ic;
    ic.n = ser_json(&s.r, (char *)ic.blob);
    for (int k = 0; k < N_INTEG; k++) {
        integ_init(&ic.g, k, g_k0);
        time_op(o, INTEG_S[k], "L2", op_integ, &ic, R);
    }
    Integ ag; integ_init(&ag, I_FSSAGG, g_k0);
    time_op(o, "anchor_event", "-", op_anchor, &ag, R);
    static LogCtx lc;
    lc.n = ic.n; memcpy(lc.blob, ic.blob, lc.n);
    char p[700];
    for (int w = 0; w < N_WPOL; w++) {
        snprintf(p, sizeof p, "%s/e1_append.log", logdir());
        log_open(&lc.L, p, w);
        int Rw = (w == W_FSYNC1 || w == W_FSYNC10) ? (R < 30 ? R : 30) : R;
        time_op(o, "append", WPOL_S[w], op_append, &lc, Rw);
        log_close(&lc.L);
        unlink(p);
    }
    fclose(o);
    free(cb);
}

/* ========================================================= E2: end to end */

typedef struct {
    int level, integ, wpol;
    const char *name;
} Cfg;

static void e2_e2e(int reps, int ndec) {
    FILE *o = openw("e2_e2e_raw.csv");
    fprintf(o, "config,level,integrity,durability,rep,decision,inference_ns,evidence_ns\n");
    Cfg cfgs[] = {
        {L2, I_FSHMAC, W_FSYNC100, "default"},
        {L1, I_FSHMAC, W_FSYNC100, "level-L1"},
        {L3, I_FSHMAC, W_FSYNC100, "level-L3"},
        {L2, I_CHAIN, W_FSYNC100, "integ-chain"},
        {L2, I_FSSAGG, W_FSYNC100, "integ-fssagg"},
        {L2, I_ED25519, W_FSYNC100, "integ-ed25519"},
        {L2, I_FSHMAC, W_BUFFERED, "dur-buffered"},
        {L2, I_FSHMAC, W_FSYNC1, "dur-fsync1"},
    };
    int nc = sizeof cfgs / sizeof cfgs[0];
    double *cb = malloc(8 * D);
    char *buf = malloc(16384);
    uint8_t tag[64];
    char p[700];
    snprintf(p, sizeof p, "%s/e2.log", logdir());
    for (int rep = 0; rep < reps; rep++) {
        for (int ci = 0; ci < nc; ci++) {
            int c = (ci + rep) % nc;              /* rotate order per repetition */
            Cfg *k = &cfgs[c];
            int nd = k->wpol == W_FSYNC1 ? ndec / 10 : ndec;
            Integ g; integ_init(&g, k->integ, g_k0);
            Log L; log_open(&L, p, k->wpol);
            Drift dm; drift_init(&dm, D, TSTAT, TSTAT + D, 3.0, 6.0);
            Rec r; memset(&r, 0, sizeof r);
            strcpy(r.model_version, MODEL_VERSION);
            r.level = k->level; r.d = D;
            for (int i = 0; i < nd; i++) {
                const double *x = X + (size_t)(i % NROWS) * D;
                uint64_t t0 = now_ns();
                double pr = predict_tl(x, D);
                uint64_t t1 = now_ns();
                r.seq = i; r.ts = t1; r.x = x;
                r.decision = pr >= 0.5; r.conf = r.decision ? pr : 1 - pr;
                r.drift = drift_update(&dm, x);
                r.response = r.drift == 2 ? 2 : 0;
                hmac_fast(&g_dig, x, 8 * D, r.digest);
                if (k->level >= L2) { saabas(&F, x, cb); topk(cb, D, r.tk_idx, r.tk_val); }
                memcpy(r.prev, g.head, 32);
                size_t n = ser_json(&r, buf);
                size_t tl = integ_link(&g, (uint8_t *)buf, n, tag);
                log_append(&L, buf, n, tag, tl);
                if ((i + 1) % ANCHOR_EVERY == 0) { Anchor a; anchor_emit(&g, i + 1, &a); }
                uint64_t t2 = now_ns();
                if (i >= (nd / 10 < 200 ? nd / 10 : 200))  /* warmup */
                    fprintf(o, "%s,%d,%s,%s,%d,%d,%llu,%llu\n", k->name, k->level, INTEG_S[k->integ],
                            WPOL_S[k->wpol], rep, i, (unsigned long long)(t1 - t0),
                            (unsigned long long)(t2 - t1));
            }
            log_close(&L);
            unlink(p);
        }
        fprintf(stderr, "  e2 rep %d/%d\n", rep + 1, reps);
    }
    fclose(o);
    free(cb); free(buf);
}

/* ========================================================= E3: sizes */

static void e3_sizes(void) {
    FILE *o = openw("e3_sizes.csv");
    fprintf(o, "features,level,encoding,mean_bytes,hash_field_bytes\n");
    int widths[] = {D, 20, 100, 500};
    char *jb = malloc(65536); uint8_t *bb = malloc(65536);
    double *xs = malloc(8 * 500);
    for (int wi = 0; wi < 4; wi++) {
        int d = widths[wi];
        for (int L = L1; L <= L3; L++) {
            double sj = 0, sb = 0;
            int n = 2000;
            for (int s = 0; s < n; s++) {
                Rec r; memset(&r, 0, sizeof r);
                strcpy(r.model_version, MODEL_VERSION);
                r.level = L; r.d = d; r.seq = 1000000 + s; r.ts = now_ns();
                const double *src = X + (size_t)(s % NROWS) * D;
                for (int j = 0; j < d; j++) xs[j] = src[j % D];   /* real values, tiled for wider d */
                r.x = xs; r.decision = s & 1; r.conf = 0.5 + (s % 500) / 1000.0;
                RAND_bytes(r.digest, 32); RAND_bytes(r.prev, 32);
                for (int k = 0; k < K_TOP; k++) { r.tk_idx[k] = (s + k) % d; r.tk_val[k] = 0.01 * (k + 1); }
                sj += ser_json(&r, jb); sb += ser_bin(&r, bb);
            }
            /* hash fields: JSON = 2 x (key + 64 hex + quotes/colon/comma); binary = 2 x 32 */
            fprintf(o, "%d,L%d,json-hex,%.1f,%d\n", d, L, sj / n,
                    (int)(strlen("\"input_digest\":\"\",") + strlen("\"prev_hash\":\"\",") + 128));
            fprintf(o, "%d,L%d,binary-raw,%.1f,%d\n", d, L, sb / n, 64);
        }
    }
    fclose(o);
    free(jb); free(bb); free(xs);
}

/* ========================================================= E4: tamper tests */

/* In-memory log for the tamper tests */
typedef struct {
    int n;
    char *blob[2048];
    size_t len[2048];
    uint8_t tag[2048][64];
    size_t tl[2048];
    uint8_t agg[32];          /* FssAgg aggregate as stored on the node */
    Anchor anchors[32];
    int n_anchor;
} MemLog;

static Rec mk_rec(int seq) {
    Rec r; memset(&r, 0, sizeof r);
    strcpy(r.model_version, MODEL_VERSION);
    r.level = L1; r.d = D; r.seq = seq; r.ts = 1790000000000000000ull + 1000000ull * seq;
    r.decision = seq % 3 == 0; r.conf = 0.9;
    hmac_fast(&g_dig, X + (size_t)(seq % NROWS) * D, 8 * D, r.digest);
    return r;
}

/* honest node writes n records; anchors every ANCHOR_EVERY records */
static void build_memlog(MemLog *M, int kind, int n, Integ *g_out_state) {
    memset(M, 0, sizeof *M);
    Integ g; integ_init(&g, kind, g_k0);
    char buf[4096];
    for (int i = 0; i < n; i++) {
        Rec r = mk_rec(i);
        memcpy(r.prev, g.head, 32);
        size_t len = ser_json(&r, buf);
        M->tl[i] = integ_link(&g, (uint8_t *)buf, len, M->tag[i]);
        M->blob[i] = strndup(buf, len); M->len[i] = len;
        if ((i + 1) % ANCHOR_EVERY == 0) anchor_emit(&g, i + 1, &M->anchors[M->n_anchor++]);
    }
    M->n = n;
    memcpy(M->agg, g.agg, 32);
    *g_out_state = g;            /* what a node compromised now holds */
}

static int json_seq(const char *b) { return atoi(strstr(b, "\"seq\":") + 6); }

/* auditor replay. Returns 1 if the log verifies. The auditor holds K0
 * (MAC variants), the node public key, and the anchors received by the
 * orchestrator. */
static int verify_memlog(const MemLog *M, int kind) {
    Integ g; integ_init(&g, kind, g_k0);
    int a = 0;
    for (int i = 0; i < M->n; i++) {
        if (json_seq(M->blob[i]) != i) return 0;
        char want[65]; hexcat(want, g.head); want[64] = 0;
        const char *ph = strstr(M->blob[i], "\"prev_hash\":\"");
        if (!ph || strncmp(ph + 13, want, 64)) return 0;
        uint8_t tag[64];
        size_t tl;
        if (kind == I_ED25519) {
            sha256(M->blob[i], M->len[i], g.head);
            if (M->tl[i] != 64 || !ed_verify(g.head, 32, M->tag[i])) return 0;
        } else {
            tl = integ_link(&g, (const uint8_t *)M->blob[i], M->len[i], tag);
            if (tl && (M->tl[i] != tl || memcmp(tag, M->tag[i], tl))) return 0;
        }
        while (a < M->n_anchor && M->anchors[a].count == (uint64_t)(i + 1)) {
            if (memcmp(M->anchors[a].head, g.head, 32)) return 0;
            if (kind == I_FSSAGG && memcmp(M->anchors[a].agg, g.agg, 32)) return 0;
            a++;
        }
    }
    if (a < M->n_anchor) return 0;             /* anchored records missing */
    if (kind == I_FSSAGG && memcmp(g.agg, M->agg, 32)) return 0;
    return 1;
}

/* Re-link records [from, n) after an edit, with the key material the
 * attacker holds. mode: 0 = storage attacker (no keys, recomputes hashes
 * only), 1 = node compromised at end of run (current keys), 2 = auditor
 * holding K0 (MAC variants). */
static void relink(MemLog *M, int kind, int from, int mode, const Integ *node_now) {
    Integ g; integ_init(&g, kind, g_k0);
    /* replay the untouched prefix to recover the chain state at `from` */
    for (int i = 0; i < from; i++) {
        uint8_t t[64];
        if (kind == I_ED25519) sha256(M->blob[i], M->len[i], g.head);
        else integ_link(&g, (uint8_t *)M->blob[i], M->len[i], t);
    }
    if (mode == 0) memset(g.key, 0xAA, 32);              /* storage attacker: no key */
    if (mode == 1 && kind != I_ED25519) memcpy(g.key, node_now->key, 32);
    /* mode 2: g.key is the auditor's correctly evolved key; mode 1 with
     * ed25519 uses the node's signing key, which a compromised node holds */
    char buf[4096];
    for (int i = from; i < M->n; i++) {
        char *b = M->blob[i];
        char *ph = strstr(b, "\"prev_hash\":\"");
        hexcat(ph + 13, g.head);
        /* renumber seq so the sequence stays contiguous */
        Rec dummy; (void)dummy;
        char *sq = strstr(b, "\"seq\":");
        int old = atoi(sq + 6);
        if (old != i) {
            char rest[4096];
            strcpy(rest, strchr(sq + 6, ','));
            sprintf(sq + 6, "%d%s", i, rest);
        }
        M->len[i] = strlen(b);
        if (kind == I_ED25519) {
            sha256(b, M->len[i], g.head);
            if (mode == 1) M->tl[i] = ed_sign(g.head, 32, M->tag[i]);
            /* modes 0/2 cannot sign: old signature stays */
        } else {
            M->tl[i] = integ_link(&g, (uint8_t *)b, M->len[i], M->tag[i]);
        }
        (void)buf;
    }
    /* the node-side aggregate: a storage attacker cannot recompute it;
     * a compromised node or the auditor can only produce it with keys */
    if (kind == I_FSSAGG && mode != 0) memcpy(M->agg, g.agg, 32);
}

static void flip_decision(char *b) {
    char *d = strstr(b, "\"decision\":");
    d[11] = d[11] == '1' ? '0' : '1';
}
static void set_model_version(MemLog *M, int i, const char *v) {
    char *b = M->blob[i], *p = strstr(b, MODEL_VERSION);
    char out[4096];
    size_t pre = p - b;
    memcpy(out, b, pre);
    sprintf(out + pre, "%s%s", v, p + strlen(MODEL_VERSION));
    free(M->blob[i]);
    M->blob[i] = strdup(out);
    M->len[i] = strlen(out);
}
static void copy_memlog(MemLog *dst, const MemLog *src) {
    *dst = *src;
    for (int i = 0; i < src->n; i++) {
        dst->blob[i] = malloc(4096);
        memcpy(dst->blob[i], src->blob[i], src->len[i] + 1);
    }
}
static void free_memlog(MemLog *M) { for (int i = 0; i < M->n; i++) free(M->blob[i]); }

static void insert_record(MemLog *M, int at, int kind) {
    for (int i = M->n; i > at; i--) {
        M->blob[i] = M->blob[i - 1]; M->len[i] = M->len[i - 1];
        memcpy(M->tag[i], M->tag[i - 1], 64); M->tl[i] = M->tl[i - 1];
    }
    Rec r = mk_rec(at);
    strcpy(r.model_version, "rf-unsw15-v2");      /* back-dated model update */
    char buf[4096];
    size_t n = ser_json(&r, buf);
    M->blob[at] = malloc(4096); memcpy(M->blob[at], buf, n + 1); M->len[at] = n;
    M->n++;
    (void)kind;
}

static void e4_tamper(void) {
    FILE *o = openw("e4_tamper.csv");
    fprintf(o, "integrity,attack,attacker,region,detected\n");
    const int N = 1030;     /* anchors at 100..1000: last 30 records unanchored */
    for (int k = 0; k < N_INTEG; k++) {
        MemLog base; Integ node_now;
        build_memlog(&base, k, N, &node_now);
        if (!verify_memlog(&base, k)) DIE("honest log does not verify (%s)", INTEG_S[k]);
        /* key erasure check: no earlier key equals the current one and the
         * current state holds only K_n (sanity check, not a proof) */
        struct { const char *atk, *who, *reg; } T[] = {
            {"modify-decision", "storage", "anchored"},
            {"modify-decision", "storage", "unanchored"},
            {"delete-record", "storage", "anchored"},
            {"delete-record", "storage", "unanchored"},
            {"reorder-records", "storage", "unanchored"},
            {"backdate-model-version", "storage", "anchored"},
            {"insert-backdated-model-update", "storage", "unanchored"},
            {"truncate-tail", "storage", "unanchored"},
            {"rewrite-suffix", "node-compromise", "unanchored"},
            {"truncate-tail", "node-compromise", "unanchored"},
            {"forge-record", "auditor-with-K0", "unanchored"},
        };
        int nt = sizeof T / sizeof T[0];
        for (int t = 0; t < nt; t++) {
            MemLog M; copy_memlog(&M, &base);
            int at = strcmp(T[t].reg, "anchored") ? N - 15 : 450;
            int mode = !strcmp(T[t].who, "storage") ? 0 : !strcmp(T[t].who, "node-compromise") ? 1 : 2;
            const char *a = T[t].atk;
            if (!strcmp(a, "modify-decision") || !strcmp(a, "rewrite-suffix") || !strcmp(a, "forge-record")) {
                flip_decision(M.blob[at]); relink(&M, k, at, mode, &node_now);
            } else if (!strcmp(a, "delete-record")) {
                free(M.blob[at]);
                for (int i = at; i < M.n - 1; i++) {
                    M.blob[i] = M.blob[i + 1]; M.len[i] = M.len[i + 1];
                    memcpy(M.tag[i], M.tag[i + 1], 64); M.tl[i] = M.tl[i + 1];
                }
                M.n--;
                relink(&M, k, at, mode, &node_now);
            } else if (!strcmp(a, "reorder-records")) {
                char *tb = M.blob[at]; M.blob[at] = M.blob[at + 1]; M.blob[at + 1] = tb;
                size_t tl = M.len[at]; M.len[at] = M.len[at + 1]; M.len[at + 1] = tl;
                relink(&M, k, at, mode, &node_now);
            } else if (!strcmp(a, "backdate-model-version")) {
                set_model_version(&M, at, "rf-unsw15-v2");
                relink(&M, k, at, mode, &node_now);
            } else if (!strcmp(a, "insert-backdated-model-update")) {
                insert_record(&M, at, k);
                relink(&M, k, at, mode, &node_now);
            } else if (!strcmp(a, "truncate-tail")) {
                for (int i = N - 20; i < M.n; i++) free(M.blob[i]);
                M.n = N - 20;
                if (k == I_FSSAGG && mode == 1) {
                    /* a compromised node holds only K_n and the current
                     * aggregate; the aggregate of the shorter prefix was
                     * overwritten and cannot be recomputed without the
                     * erased per-record tags. Best effort: keep it. */
                }
            }
            int ok = verify_memlog(&M, k);
            fprintf(o, "%s,%s,%s,%s,%d\n", INTEG_S[k], a, T[t].who, T[t].reg, !ok);
            M.n = M.n; free_memlog(&M);
        }
        free_memlog(&base);
    }
    fclose(o);
}

/* ========================================================= E5: verification */

static void e5_verify(void) {
    FILE *o = openw("e5_verify.csv");
    fprintf(o, "integrity,records,ns_per_record\n");
    for (int k = 0; k < N_INTEG; k++) {
        MemLog M; Integ s;
        build_memlog(&M, k, 2000, &s);
        for (int rep = 0; rep < (g_quick ? 3 : 15); rep++) {
            uint64_t t0 = now_ns();
            if (!verify_memlog(&M, k)) DIE("verify failed");
            fprintf(o, "%s,%d,%.1f\n", INTEG_S[k], M.n, (double)(now_ns() - t0) / M.n);
        }
        free_memlog(&M);
    }
    fclose(o);
}

/* ========================================================= E6: drift */

static void e6_drift(const char *build) {
    /* Layout of drift_streams.bin: mu[d], sd[d] (normal training traffic),
     * then a calibration stream of normal TRAINING rows, then for each
     * scenario phase A (normal TEST rows) followed by phase B (one attack
     * category, TEST rows). The alarm threshold is fixed on the
     * calibration stream only: 1.05 x its maximum score. */
    char p[700];
    snprintf(p, sizeof p, "%s/drift_streams.json", build);
    size_t jl; char *js = slurp(p, &jl);
    snprintf(p, sizeof p, "%s/drift_streams.bin", build);
    size_t bl; double *db = slurp(p, &bl);
    double *mu = db, *sd = db + D, *s = db + 2 * D;
    int ncal = atoi(strstr(js, "\"calib_rows\": ") + 14);
    Drift m; drift_init(&m, D, mu, sd, 1e300, 1e300);
    double cmax = 0;
    for (int i = 0; i < ncal; i++) {
        drift_update(&m, s + (size_t)i * D);
        for (int j = 0; j < D; j++) {
            double z = fabs(m.ew[j] - m.mu[j]) / m.sd[j];
            if (i >= 200 && z > cmax) cmax = z;   /* skip EWMA burn-in */
        }
    }
    s += (size_t)ncal * D;
    double thr = 1.05 * cmax;
    FILE *o = openw("e6_drift.csv");
    fprintf(o, "category,threshold,calib_max_score,phase_a,phase_b,false_alarm_at,detected_after,max_score_a,max_score_b\n");
    char *q = js;
    while ((q = strstr(q, "\"category\": \""))) {
        char cat[64]; int pa, pb;
        sscanf(q + 13, "%63[^\"]", cat);
        pa = atoi(strstr(q, "\"phase_a\": ") + 11);
        pb = atoi(strstr(q, "\"phase_b\": ") + 11);
        Drift dm; drift_init(&dm, D, mu, sd, thr / 2, thr);
        int fa = -1, det = -1;
        double ma = 0, mb = 0;
        for (int i = 0; i < pa + pb; i++) {
            int st = drift_update(&dm, s + (size_t)i * D);
            double sc = 0;
            for (int j = 0; j < D; j++) { double z = fabs(dm.ew[j] - dm.mu[j]) / dm.sd[j]; if (z > sc) sc = z; }
            if (i < pa) { if (i >= 200 && sc > ma) ma = sc; if (st == 2 && i >= 200 && fa < 0) fa = i; }
            else { if (sc > mb) mb = sc; if (st == 2 && det < 0) det = i - pa; }
        }
        fprintf(o, "%s,%.4g,%.4g,%d,%d,%d,%d,%.4g,%.4g\n", cat, thr, cmax, pa, pb, fa, det, ma, mb);
        s += (size_t)(pa + pb) * D;
        q += 13;
    }
    fclose(o);
}

/* ================================================================ main */

int main(int argc, char **argv) {
    if (argc < 4) DIE("usage: bench <build_dir> <trees> <out_dir> [--quick] [--only E1,E2,..]");
    const char *build = argv[1];
    int trees = atoi(argv[2]);
    snprintf(g_out, sizeof g_out, "%s", argv[3]);
    const char *only = "E1,E2,E3,E4,E5,E6";
    for (int i = 4; i < argc; i++) {
        if (!strcmp(argv[i], "--quick")) g_quick = 1;
        if (!strcmp(argv[i], "--only") && i + 1 < argc) only = argv[++i];
    }
    mkdir(g_out, 0755);
    char p[700];
    snprintf(p, sizeof p, "%s/forest_T%d.bin", build, trees);
    F = load_forest(p);
    D = F.d;
    if (get_num_feature() != D) DIE("compiled model has %d features, forest file %d", get_num_feature(), D);
    g_entry = calloc(D, sizeof(union Entry));
    size_t len;
    snprintf(p, sizeof p, "%s/stream.bin", build);
    X = slurp(p, &len); NROWS = (int)(len / (8 * D));
    snprintf(p, sizeof p, "%s/ref_proba_T%d.bin", build, trees);
    REF = slurp(p, &len);
    snprintf(p, sizeof p, "%s/train_stats.bin", build);
    TSTAT = slurp(p, &len);

    /* keys */
    uint8_t dkey[32];
    RAND_bytes(dkey, 32); hmac_prep(&g_dig, dkey); explicit_bzero(dkey, 32);
    RAND_bytes(g_k0, 32);
    EVP_PKEY_CTX *kc = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, NULL);
    EVP_PKEY_keygen_init(kc); EVP_PKEY_keygen(kc, &g_sk); EVP_PKEY_CTX_free(kc);
    int sv[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    g_anchor_fd = sv[0];
    pthread_t th; pthread_create(&th, NULL, drain, &sv[1]);
    drift_init(&g_drift, D, TSTAT, TSTAT + D, 3.0, 6.0);

    /* parity: compiled forest and reference walk against sklearn */
    double md_tl = 0, md_ref = 0, mx_saabas = 0;
    double *cb = malloc(8 * D);
    for (int i = 0; i < NROWS; i++) {
        const double *x = X + (size_t)i * D;
        double a = fabs(predict_tl(x, D) - REF[i]), b = fabs(predict_ref(&F, x) - REF[i]);
        if (a > md_tl) md_tl = a;
        if (b > md_ref) md_ref = b;
        if (i < 2000) {  /* Saabas exactness: root mean + sum(contrib) = output */
            saabas(&F, x, cb);
            double s = 0; for (int j = 0; j < D; j++) s += cb[j];
            double root = 0; for (int t = 0; t < F.n_trees; t++) root += F.t[t].p1[0];
            double e = fabs(root / F.n_trees + s - predict_ref(&F, x));
            if (e > mx_saabas) mx_saabas = e;
        }
    }
    FILE *m = openw("bench_meta.json");
    fprintf(m, "{\"trees\":%d,\"features\":%d,\"stream_rows\":%d,"
               "\"parity_max_abs_diff_treelite_vs_sklearn\":%.3g,"
               "\"parity_max_abs_diff_refwalk_vs_sklearn\":%.3g,"
               "\"saabas_max_abs_reconstruction_error\":%.3g,"
               "\"openssl\":\"%s\",\"quick\":%d}\n",
            trees, D, NROWS, md_tl, md_ref, mx_saabas, OpenSSL_version(0), g_quick);
    fclose(m);
    fprintf(stderr, "T=%d d=%d rows=%d parity tl=%.2g ref=%.2g saabas=%.2g\n",
            trees, D, NROWS, md_tl, md_ref, mx_saabas);
    if (md_tl > 1e-9) DIE("compiled forest does not match sklearn");

    int R = g_quick ? 21 : 201;
    if (strstr(only, "E1")) { fprintf(stderr, "E1 ops\n"); e1_ops(R); }
    if (strstr(only, "E2")) { fprintf(stderr, "E2 end-to-end\n"); e2_e2e(g_quick ? 2 : 10, g_quick ? 2000 : 20000); }
    if (strstr(only, "E3")) { fprintf(stderr, "E3 sizes\n"); e3_sizes(); }
    if (strstr(only, "E4")) { fprintf(stderr, "E4 tamper\n"); e4_tamper(); }
    if (strstr(only, "E5")) { fprintf(stderr, "E5 verify\n"); e5_verify(); }
    if (strstr(only, "E6")) { fprintf(stderr, "E6 drift\n"); e6_drift(build); }
    close(sv[0]);
    pthread_join(th, NULL);
    return 0;
}
