/*
 * tb_eeg_filter.c - host test bench: CMSIS-DSP eeg_filter vs MATLAB golden vectors.
 *
 * Module : eeg_filter (task #17, flow 05-Experimental/flow-dsp-filter)
 * Author : Tran Khoa Vu (tran-khoa-vu-k68), with Claude Code
 * Date   : 2026-11-09
 *
 * Usage  : tb_eeg_filter <vector_dir> <variant_name> <out_json>
 * Inputs : <vector_dir>/meta.txt, codes_i32.bin, <family>_coeffs_f32.bin, <family>_y_f32.bin,
 *          <family>_y_f64.bin, long_codes_i32.bin, long_<selected>_y_f32.bin / _y_f64.bin
 *          written by scripts/+eegfilt/run_config.m.
 * Checks : F6 one-shot output bit-exact vs MATLAB f32 emulation (0 mismatches)
 *          F4 float32 vs float64 reference after settling (RMSE, max |e|)
 *          F5 fixed blocks and seeded random packets bit-exact vs one-shot
 *          F7 long run (selected family): bit-exact, last-window error, zero-input tail
 *          ADS1299 24-bit sign extension
 * Output : JSON rows for scripts/summarize.py; one line per test:
 *          TEST <name> PASS|FAIL mismatches=<m> samples=<n>
 * Exit   : 0 if every check passes, 1 otherwise, 2 on I/O or usage errors.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "eeg_filter.h"

#define MAX_FAMILIES 8
#define MAX_BLOCKS 16
#define NAME_LEN 64
#define PATH_LEN 1024

typedef struct {
    char config[NAME_LEN];
    char families[MAX_FAMILIES][NAME_LEN];
    int num_families;
    char selected[NAME_LEN];
    int num_stages;
    long n;
    long settle_idx;
    uint32_t lsb_bits;
    long block_sizes[MAX_BLOCKS];
    int num_blocks;
    unsigned long packet_seed;
    long packet_max;
    double f4_rmse_max;
    double f4_abs_max;
    long long_n;
    long long_signal_n;
    long long_eval_idx;
    long tail_idx;
    double tail_abs_max;
} meta_t;

static FILE *g_json;
static int g_first_row = 1;
static int g_fail;

static void die(const char *msg, const char *arg)
{
    fprintf(stderr, "tb_eeg_filter: %s %s\n", msg, arg ? arg : "");
    exit(2);
}

static void *load_bin(const char *dir, const char *name, size_t elem, long count)
{
    char path[PATH_LEN];
    FILE *fp;
    void *buf;
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    fp = fopen(path, "rb");
    if (!fp) {
        die("cannot open", path);
    }
    buf = malloc(elem * (size_t)count);
    if (!buf) {
        die("out of memory for", path);
    }
    if (fread(buf, elem, (size_t)count, fp) != (size_t)count) {
        die("short read", path);
    }
    fclose(fp);
    return buf;
}

static void read_meta(const char *dir, meta_t *m)
{
    char path[PATH_LEN];
    char line[1024];
    FILE *fp;
    memset(m, 0, sizeof(*m));
    snprintf(path, sizeof(path), "%s/meta.txt", dir);
    fp = fopen(path, "r");
    if (!fp) {
        die("cannot open", path);
    }
    while (fgets(line, sizeof(line), fp)) {
        char key[NAME_LEN];
        int off = 0;
        if (sscanf(line, "%63s %n", key, &off) != 1) {
            continue;
        }
        char *v = line + off;
        if (!strcmp(key, "config")) {
            sscanf(v, "%63s", m->config);
        } else if (!strcmp(key, "families")) {
            int used = 0;
            while (m->num_families < MAX_FAMILIES &&
                   sscanf(v, "%63s%n", m->families[m->num_families], &used) == 1) {
                m->num_families++;
                v += used;
            }
        } else if (!strcmp(key, "selected")) {
            sscanf(v, "%63s", m->selected);
        } else if (!strcmp(key, "num_stages")) {
            m->num_stages = atoi(v);
        } else if (!strcmp(key, "n")) {
            m->n = atol(v);
        } else if (!strcmp(key, "settle_idx")) {
            m->settle_idx = atol(v);
        } else if (!strcmp(key, "lsb_hex")) {
            m->lsb_bits = (uint32_t)strtoul(v, NULL, 16);
        } else if (!strcmp(key, "block_sizes")) {
            int used = 0;
            while (m->num_blocks < MAX_BLOCKS &&
                   sscanf(v, "%ld%n", &m->block_sizes[m->num_blocks], &used) == 1) {
                m->num_blocks++;
                v += used;
            }
        } else if (!strcmp(key, "packet_seed")) {
            m->packet_seed = strtoul(v, NULL, 10);
        } else if (!strcmp(key, "packet_max")) {
            m->packet_max = atol(v);
        } else if (!strcmp(key, "f4_rmse_max")) {
            m->f4_rmse_max = atof(v);
        } else if (!strcmp(key, "f4_abs_max")) {
            m->f4_abs_max = atof(v);
        } else if (!strcmp(key, "long_n")) {
            m->long_n = atol(v);
        } else if (!strcmp(key, "long_signal_n")) {
            m->long_signal_n = atol(v);
        } else if (!strcmp(key, "long_eval_idx")) {
            m->long_eval_idx = atol(v);
        } else if (!strcmp(key, "tail_idx")) {
            m->tail_idx = atol(v);
        } else if (!strcmp(key, "tail_abs_max")) {
            m->tail_abs_max = atof(v);
        }
    }
    fclose(fp);
    if (m->num_families == 0 || m->num_stages <= 0 || m->n <= 0 || m->packet_max <= 0) {
        die("incomplete meta.txt in", dir);
    }
}

static long count_mismatch(const float32_t *a, const float32_t *b, long n)
{
    long i, k = 0;
    for (i = 0; i < n; i++) {
        uint32_t ua, ub;
        memcpy(&ua, &a[i], sizeof(ua));
        memcpy(&ub, &b[i], sizeof(ub));
        k += (ua != ub);
    }
    return k;
}

static void err_stats(const float32_t *y, const double *ref, long from, long to, double *rmse,
                      double *emax)
{
    long i;
    double s = 0.0, m = 0.0;
    for (i = from; i < to; i++) {
        double e = (double)y[i] - ref[i];
        s += e * e;
        if (fabs(e) > m) {
            m = fabs(e);
        }
    }
    *rmse = sqrt(s / (double)(to - from));
    *emax = m;
}

/* Process a stream with block edges given by a generator: fixed size or LCG random packets. */
static void run_stream(const float32_t *coeffs, int ns, float32_t lsb, const int32_t *codes,
                       long n, long fixed, unsigned long seed, long pmax, float32_t *out)
{
    eeg_filter_t f;
    float32_t *state = calloc(EEG_FILTER_STATE_LEN(ns), sizeof(float32_t));
    unsigned long lcg = seed;
    long pos = 0;
    if (!state) {
        die("out of memory", "state");
    }
    eeg_filter_init(&f, (uint8_t)ns, coeffs, state, lsb);
    while (pos < n) {
        long len;
        if (fixed > 0) {
            len = fixed;
        } else {
            lcg = (1103515245UL * lcg + 12345UL) & 0x7FFFFFFFUL;
            len = 1 + (long)(lcg % (unsigned long)pmax);
        }
        if (len > n - pos) {
            len = n - pos;
        }
        eeg_filter_process_codes(&f, codes + pos, out + pos, (uint32_t)len);
        pos += len;
    }
    free(state);
}

static void row(const char *id, const char *family, const char *metric, double value,
                const char *unit, const char *threshold, int pass)
{
    fprintf(g_json, "%s\n    {\"id\": \"%s\", \"family\": \"%s\", \"metric\": \"%s\", "
                    "\"value\": %.9g, \"unit\": \"%s\", \"threshold\": \"%s\", \"pass\": %s}",
            g_first_row ? "" : ",", id, family, metric, value, unit, threshold,
            pass ? "true" : "false");
    g_first_row = 0;
    if (!pass) {
        g_fail = 1;
    }
}

static void test_line(const char *config, const char *variant, const char *name, long mism,
                      long samples, int pass)
{
    printf("TEST eeg_filter_%s_%s_%s %s mismatches=%ld samples=%ld\n", config, variant, name,
           pass ? "PASS" : "FAIL", mism, samples);
}

static int check_sign_extension(void)
{
    const uint8_t v[4][3] = {{0x80, 0x00, 0x00}, {0x7F, 0xFF, 0xFF}, {0xFF, 0xFF, 0xFF},
                             {0x00, 0x00, 0x01}};
    const int32_t want[4] = {-8388608, 8388607, -1, 1};
    int i, bad = 0;
    for (i = 0; i < 4; i++) {
        bad += (eeg_ads1299_code(v[i]) != want[i]);
    }
    return bad;
}

int main(int argc, char **argv)
{
    meta_t m;
    float32_t lsb;
    int fi;
    char thr[96];
    const char *dir, *variant;
    int32_t *codes;
    long bad;

    if (argc != 4) {
        fprintf(stderr, "usage: %s <vector_dir> <variant_name> <out_json>\n", argv[0]);
        return 2;
    }
    dir = argv[1];
    variant = argv[2];
    read_meta(dir, &m);
    memcpy(&lsb, &m.lsb_bits, sizeof(lsb));
    codes = load_bin(dir, "codes_i32.bin", sizeof(int32_t), m.n);
    g_json = fopen(argv[3], "w");
    if (!g_json) {
        die("cannot write", argv[3]);
    }
    fprintf(g_json, "{\n  \"config\": \"%s\",\n  \"variant\": \"%s\",\n  \"rows\": [", m.config,
            variant);

    bad = check_sign_extension();
    row("ADC", "-", "ads1299_sign_extension_mismatches", (double)bad, "count", "== 0", bad == 0);
    test_line(m.config, variant, "ads1299_sign_extension", bad, 4, bad == 0);

    for (fi = 0; fi < m.num_families; fi++) {
        const char *fam = m.families[fi];
        char name[PATH_LEN];
        float32_t *coeffs, *y_ml, *y_c, *y_b;
        double *y64, rmse, emax;
        long mism;
        int b;
        clock_t t0;
        int reps = 0;

        snprintf(name, sizeof(name), "%s_coeffs_f32.bin", fam);
        coeffs = load_bin(dir, name, sizeof(float32_t), 5L * m.num_stages);
        snprintf(name, sizeof(name), "%s_y_f32.bin", fam);
        y_ml = load_bin(dir, name, sizeof(float32_t), m.n);
        snprintf(name, sizeof(name), "%s_y_f64.bin", fam);
        y64 = load_bin(dir, name, sizeof(double), m.n);
        y_c = malloc(sizeof(float32_t) * (size_t)m.n);
        y_b = malloc(sizeof(float32_t) * (size_t)m.n);
        if (!y_c || !y_b) {
            die("out of memory", fam);
        }

        /* F6: one-shot C vs MATLAB f32 emulation */
        run_stream(coeffs, m.num_stages, lsb, codes, m.n, m.n, 0UL, 1L, y_c);
        mism = count_mismatch(y_c, y_ml, m.n);
        row("F6", fam, "c_vs_matlab_f32_mismatches", (double)mism, "count", "== 0", mism == 0);
        test_line(m.config, variant, fam, mism, m.n, mism == 0);

        /* F4: C float32 vs MATLAB float64 after settling */
        err_stats(y_c, y64, m.settle_idx, m.n, &rmse, &emax);
        snprintf(thr, sizeof(thr), "<= %.6g", m.f4_rmse_max);
        row("F4", fam, "c_f32_vs_f64_rmse", rmse, "uV", thr, rmse <= m.f4_rmse_max);
        snprintf(thr, sizeof(thr), "<= %.6g", m.f4_abs_max);
        row("F4", fam, "c_f32_vs_f64_max_abs", emax, "uV", thr, emax <= m.f4_abs_max);

        /* F5: fixed blocks and random packets vs one-shot */
        for (b = 0; b < m.num_blocks; b++) {
            char metric[NAME_LEN];
            run_stream(coeffs, m.num_stages, lsb, codes, m.n, m.block_sizes[b], 0UL, 1L, y_b);
            mism = count_mismatch(y_b, y_c, m.n);
            snprintf(metric, sizeof(metric), "c_block_%ld_mismatches", m.block_sizes[b]);
            row("F5", fam, metric, (double)mism, "count", "== 0", mism == 0);
        }
        run_stream(coeffs, m.num_stages, lsb, codes, m.n, 0L, m.packet_seed, m.packet_max, y_b);
        mism = count_mismatch(y_b, y_c, m.n);
        row("F5", fam, "c_random_packet_mismatches", (double)mism, "count", "== 0", mism == 0);

        /* Host timing (informational, not an MCU figure) */
        t0 = clock();
        do {
            run_stream(coeffs, m.num_stages, lsb, codes, m.n, m.n, 0UL, 1L, y_b);
            reps++;
        } while (clock() - t0 < CLOCKS_PER_SEC / 5);
        row("PERF", fam, "host_ns_per_sample",
            1e9 * (double)(clock() - t0) / CLOCKS_PER_SEC / ((double)reps * (double)m.n), "ns",
            "", 1);

        /* F7: long run for the selected family */
        if (!strcmp(fam, m.selected) && m.long_n > 0) {
            int32_t *lc = load_bin(dir, "long_codes_i32.bin", sizeof(int32_t), m.long_n);
            float32_t *ly_ml, *ly_c, *ly_b;
            double *ly64, tail = 0.0;
            long i, nonfinite = 0;
            snprintf(name, sizeof(name), "long_%s_y_f32.bin", fam);
            ly_ml = load_bin(dir, name, sizeof(float32_t), m.long_n);
            snprintf(name, sizeof(name), "long_%s_y_f64.bin", fam);
            ly64 = load_bin(dir, name, sizeof(double), m.long_n);
            ly_c = malloc(sizeof(float32_t) * (size_t)m.long_n);
            ly_b = malloc(sizeof(float32_t) * (size_t)m.long_n);
            if (!ly_c || !ly_b) {
                die("out of memory", "long run");
            }
            run_stream(coeffs, m.num_stages, lsb, lc, m.long_n, m.long_n, 0UL, 1L, ly_c);
            mism = count_mismatch(ly_c, ly_ml, m.long_n);
            row("F7", fam, "c_long_vs_matlab_f32_mismatches", (double)mism, "count", "== 0",
                mism == 0);
            test_line(m.config, variant, "long_run", mism, m.long_n, mism == 0);
            run_stream(coeffs, m.num_stages, lsb, lc, m.long_n, 0L, m.packet_seed + 1UL,
                       m.packet_max, ly_b);
            mism = count_mismatch(ly_b, ly_c, m.long_n);
            row("F7", fam, "c_long_random_packet_mismatches", (double)mism, "count", "== 0",
                mism == 0);
            for (i = 0; i < m.long_n; i++) {
                nonfinite += !isfinite(ly_c[i]);
            }
            row("F7", fam, "c_long_nonfinite_samples", (double)nonfinite, "count", "== 0",
                nonfinite == 0);
            err_stats(ly_c, ly64, m.long_eval_idx, m.long_signal_n, &rmse, &emax);
            snprintf(thr, sizeof(thr), "<= %.6g", m.f4_rmse_max);
            row("F7", fam, "c_long_last_window_rmse", rmse, "uV", thr, rmse <= m.f4_rmse_max);
            snprintf(thr, sizeof(thr), "<= %.6g", m.f4_abs_max);
            row("F7", fam, "c_long_last_window_max_abs", emax, "uV", thr, emax <= m.f4_abs_max);
            for (i = m.tail_idx; i < m.long_n; i++) {
                if (fabs((double)ly_c[i]) > tail) {
                    tail = fabs((double)ly_c[i]);
                }
            }
            snprintf(thr, sizeof(thr), "<= %.6g", m.tail_abs_max);
            row("F7", fam, "c_zero_tail_max_abs", tail, "uV", thr, tail <= m.tail_abs_max);
            free(lc);
            free(ly_ml);
            free(ly64);
            free(ly_c);
            free(ly_b);
        }
        free(coeffs);
        free(y_ml);
        free(y64);
        free(y_c);
        free(y_b);
    }
    fprintf(g_json, "\n  ],\n  \"status\": \"%s\"\n}\n", g_fail ? "FAIL" : "PASS");
    fclose(g_json);
    free(codes);
    printf("TB %s %s: %s\n", m.config, variant, g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}
