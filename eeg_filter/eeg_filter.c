/*
 * eeg_filter.c - see eeg_filter.h.
 *
 * Module : eeg_filter (task #17)
 * Author : Tran Khoa Vu (tran-khoa-vu-k68), with Claude Code
 * Date   : 2026-11-09
 */
#include "eeg_filter.h"

#include <string.h>

void eeg_filter_init(eeg_filter_t *f, uint8_t num_stages, const float32_t *coeffs,
                     float32_t *state, float32_t lsb_uv)
{
    f->lsb_uv = lsb_uv;
    f->num_stages = num_stages;
    arm_biquad_cascade_df1_init_f32(&f->biquad, num_stages, coeffs, state);
    eeg_filter_reset(f);
}

void eeg_filter_reset(eeg_filter_t *f)
{
    memset(f->biquad.pState, 0, sizeof(float32_t) * EEG_FILTER_STATE_LEN(f->num_stages));
    f->code0 = 0;
    f->has_code0 = 0U;
}

void eeg_filter_process_codes(eeg_filter_t *f, const int32_t *codes, float32_t *out, uint32_t n)
{
    uint32_t i;

    if (n == 0U) {
        return;
    }
    if (!f->has_code0) {
        f->code0 = codes[0];
        f->has_code0 = 1U;
    }
    for (i = 0U; i < n; i++) {
        /* Exact int32 difference (24-bit inputs), exact int->float conversion (< 2^24),
         * then one float32 rounding in the multiply. */
        out[i] = (float32_t)(codes[i] - f->code0) * f->lsb_uv;
    }
    /* CMSIS DF1 f32 reads each input before writing the same index, so in-place is safe. */
    arm_biquad_cascade_df1_f32(&f->biquad, out, out, n);
}
