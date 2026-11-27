/*
 * main.c - eeg_filter benchmark on the nRF52840 DK (Cortex-M4F @ 64 MHz).
 *
 * Module : eeg_filter_bench (task #17, flow 05-Experimental/flow-fw-dsp-bench)
 * Author : Tran Khoa Vu (tran-khoa-vu-k68), with Claude Code
 * Date   : 2026-11-10
 *
 * 1. Correctness: one channel, the golden ADS1299 codes from flow-dsp-filter, compared with the
 *    MATLAB float32 output (bit patterns) and the float64 reference (error in nV).
 * 2. Timing: NCH channels processed block by block for several block sizes, DWT cycle counter,
 *    interrupts locked during each measurement.
 * Output lines on the console (115200 baud) are parsed by the flow:
 *   RESULT check ...   RESULT timing ...   BENCH done
Thứ tự thực hiện :
1. Chạy firmware ADS1299 riêng để xác nhận đọc được ID và dữ liệu ADC.
2. Chạy benchmark riêng để xác nhận thuật toán lọc hoạt động chính xác và đủ nhanh.
3. Sau khi cả hai hoạt động tốt, tích hợp thành firmware EEG realtime
 */
#include <math.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#if __has_include(<cmsis_core.h>)
#include <cmsis_core.h>
#else
#include <zephyr/arch/arm/cortex_m/cmsis.h>
#endif

#include "eeg_filter.h"
#include "cmsis_filter_coeffs.h"
#include "bench_vectors.h"

#define NCH 8U
#define N_REPEAT 3U

static float32_t g_state[NCH][EEG_FILTER_STATE_LEN(BENCH_NUM_STAGES)];
static eeg_filter_t g_filt[NCH];
static float32_t g_out[BENCH_N];

static const uint32_t k_blocks[] = {1U, 5U, 25U, 50U, 250U};

static void cycles_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void filters_init(void)
{
    for (uint32_t ch = 0U; ch < NCH; ch++) {
        eeg_filter_init(&g_filt[ch], (uint8_t)BENCH_NUM_STAGES, BENCH_COEFFS, g_state[ch],
                        EEG_ADC_LSB_UV);
    }
}

static void check_correctness(void)
{
    uint32_t mism = 0U;
    double se = 0.0, emax = 0.0;

    filters_init();
    eeg_filter_process_codes(&g_filt[0], bench_codes, g_out, BENCH_N);
    for (uint32_t i = 0U; i < BENCH_N; i++) {
        uint32_t bits;
        memcpy(&bits, &g_out[i], sizeof(bits));
        mism += (bits != bench_y_f32_bits[i]);
        if (i >= BENCH_SETTLE_IDX) {
            double e = (double)g_out[i] - bench_y_f64[i];
            se += e * e;
            if (fabs(e) > emax) {
                emax = fabs(e);
            }
        }
    }
    double rmse = sqrt(se / (double)(BENCH_N - BENCH_SETTLE_IDX));
    printk("RESULT check mismatches=%u samples=%u rmse_pv=%u max_pv=%u\n", mism, (unsigned)BENCH_N,
           (unsigned)(rmse * 1e6 + 0.5), (unsigned)(emax * 1e6 + 0.5));
}

static void measure(uint32_t block)
{
    uint32_t best = UINT32_MAX, worst_block = 0U;

    for (uint32_t r = 0U; r < N_REPEAT; r++) {
        filters_init();
        uint32_t total = 0U;
        for (uint32_t pos = 0U; pos < BENCH_N; pos += block) {
            uint32_t len = (BENCH_N - pos < block) ? (BENCH_N - pos) : block;
            unsigned int key = irq_lock();
            uint32_t t0 = DWT->CYCCNT;
            for (uint32_t ch = 0U; ch < NCH; ch++) {
                eeg_filter_process_codes(&g_filt[ch], bench_codes + pos, g_out + pos, len);
            }
            uint32_t dt = DWT->CYCCNT - t0;
            irq_unlock(key);
            total += dt;
            if (len == block && dt > worst_block) {
                worst_block = dt;
            }
        }
        if (total < best) {
            best = total;
        }
    }
    printk("RESULT timing block=%u channels=%u samples=%u cycles_total=%u worst_block_cycles=%u\n",
           block, NCH, (unsigned)BENCH_N, best, worst_block);
}

int main(void)
{
    cycles_init();
    k_msleep(500);
    printk("BENCH start config=%s family=%s stages=%u fs=%u cpu_hz=%u fp_contract=%s\n", BENCH_CONFIG,
           BENCH_FAMILY, (unsigned)BENCH_NUM_STAGES, (unsigned)EEG_FS_HZ,
           (unsigned)SystemCoreClock, EEG_FP_CONTRACT_STR);
    check_correctness();
    for (uint32_t i = 0U; i < ARRAY_SIZE(k_blocks); i++) {
        measure(k_blocks[i]);
    }
    printk("BENCH done\n");
    return 0;
}
