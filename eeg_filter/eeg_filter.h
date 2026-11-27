/*
 * eeg_filter.h - EEG pre-processing filter chain (HPF -> notch -> LPF) on CMSIS-DSP DF1 f32.
 *
 * Module : eeg_filter (task #17, flow 05-Experimental/flow-dsp-filter)
 * Author : Tran Khoa Vu (tran-khoa-vu-k68), with Claude Code
 * Date   : 2026-11-09
 * Target : nRF52840 (Cortex-M4F, FPv4-SP) firmware and host reference build (gcc).
 *
 * Data path (one instance per channel):
 *   int32 ADS1299 code --(code - code0, exact int32)--> float32 * lsb_uv --> CMSIS biquad cascade
 *   code0 is the first code after init/reset. Subtracting it in the integer domain keeps the
 *   float32 values small (tens to hundreds of uV instead of the ~150 mV electrode offset),
 *   which is what keeps the float32 error within the issue #17 F4 limit (see flow README).
 *
 * Exception to CODE-CONVENTION section 7 (no float in bit-accurate data paths): this module
 * is the float32 reference of the CMSIS-DSP f32 path. Bit accuracy is defined against the
 * MATLAB emulation scripts/+eegfilt/df1.m, compiled with -ffp-contract=off on the host.
 *
 * Coefficients: stage-major {b0, b1, b2, a1, a2}, a1/a2 negated vs MATLAB SOS, as generated in
 * cmsis_filter_coeffs.h by the flow. State: 4 float32_t per stage, owned by the caller.
 */
#ifndef EEG_FILTER_H
#define EEG_FILTER_H

#include <stdint.h>

#include "arm_math.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    arm_biquad_casd_df1_inst_f32 biquad; /* CMSIS instance; pState points to caller memory */
    float32_t lsb_uv;                    /* ADC LSB in uV, e.g. EEG_ADC_LSB_UV */
    int32_t code0;                       /* integer offset captured from the first sample */
    uint8_t has_code0;                   /* 0 until the first sample after init/reset */
    uint8_t num_stages;
} eeg_filter_t;

/* Number of float32_t state words needed for num_stages stages (per channel). */
#define EEG_FILTER_STATE_LEN(num_stages) (4U * (uint32_t)(num_stages))

/* Sign-extend a 24-bit two's-complement ADS1299 sample (MSB first, as read over SPI). */
static inline int32_t eeg_ads1299_code(const uint8_t b[3])
{
    uint32_t u = ((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | (uint32_t)b[2];
    if (u & 0x800000UL) {
        u |= 0xFF000000UL;
    }
    return (int32_t)u;
}

/* Bind coefficients and state (EEG_FILTER_STATE_LEN(num_stages) words) and reset. */
void eeg_filter_init(eeg_filter_t *f, uint8_t num_stages, const float32_t *coeffs,
                     float32_t *state, float32_t lsb_uv);

/* Zero the filter state; the next sample becomes the new integer offset code0. */
void eeg_filter_reset(eeg_filter_t *f);

/* Filter n ADC codes into out (uV). Filter state continues across calls, so any split of a
 * stream into blocks or packets gives bit-identical output. */
void eeg_filter_process_codes(eeg_filter_t *f, const int32_t *codes, float32_t *out, uint32_t n);

#ifdef __cplusplus
}
#endif

#endif /* EEG_FILTER_H */
