# eeg_filter — chuỗi lọc EEG trên CMSIS-DSP (tham chiếu C cho nRF52840)

Task #17. Khối C dùng chung cho tham chiếu trên host và firmware nRF52840 (Cortex-M4F): đổi mã ADS1299 sang µV, rồi lọc HPF → notch → LPF bằng `arm_biquad_cascade_df1_f32` (CMSIS-DSP v1.16.2, ghim trong `08-external/deps.tsv`). Hệ số do `05-Experimental/flow-dsp-filter` sinh ra (`cmsis_filter_coeffs.h`).

| File | Nội dung |
|---|---|
| `eeg_filter.h/.c` | API theo kênh: `eeg_filter_init`, `eeg_filter_reset`, `eeg_filter_process_codes`; `eeg_ads1299_code` đổi 3 byte SPI (24 bit bù hai) sang `int32_t` |
| `tb_eeg_filter.c` | Test bench trên host: so khớp bit với vector golden MATLAB, chia block/gói, chạy 1 h, sai số so với float64; in `TEST ... PASS mismatches=0 samples=N` |
| `m4_footprint.c` | Chỉ biên dịch (không chạy) cho Cortex-M4F, 8 kênh, để đo flash/RAM bằng `arm-none-eabi-size` |

## Quy ước dữ liệu

- Hệ số: mỗi stage `{b0, b1, b2, a1, a2}`, trong đó a1, a2 **đã đổi dấu** so với SOS của MATLAB (CMSIS tính `y = b0·x + b1·x1 + b2·x2 + a1·y1 + a2·y2`). Thứ tự stage: HPF, notch, các stage LPF.
- State: mỗi kênh `4 × num_stages` `float32_t` (`EEG_FILTER_STATE_LEN`), do bên gọi cấp phát; hệ số dùng chung cho mọi kênh.
- Đầu vào: `(float)(code − code0) × lsb_uv`. `code0` là mẫu đầu tiên sau `init`/`reset`. Phép trừ làm trên `int32_t` nên chính xác tuyệt đối, và giá trị đưa vào float32 nhỏ. Nếu đưa thẳng offset điện cực ~150 mV vào float32 thì sai số lên tới cỡ µV (xem `summary.md` của flow).
- Gọi `eeg_filter_process_codes` với block bất kỳ: state được giữ giữa các lần gọi, nên cách chia block hay gói BLE không làm đổi kết quả.

## Ngoại lệ so với CODE-CONVENTION §7

§7 cấm `float` trong đường dữ liệu bit-accurate. Khối này cố ý là đường float32 của CMSIS-DSP: "bit-accurate" ở đây nghĩa là khớp bit với mô phỏng MATLAB `05-Experimental/flow-dsp-filter/scripts/+eegfilt/df1.m`, với điều kiện biên dịch `-ffp-contract=off`. Mã vẫn là C99, biên dịch sạch với `-Wall -Wextra -Werror`.

## Build

Flow tự build; xem `05-Experimental/flow-dsp-filter/README.md` mục 2 và 4. Build tay trên host (sau khi chạy `bash 08-external/fetch.sh cmsis-dsp`):

```sh
D=08-external/cmsis-dsp; F=$D/Source/FilteringFunctions
gcc -std=c99 -O2 -Wall -Wextra -Werror -ffp-contract=off -D__GNUC_PYTHON__ \
  -I$D/Include -I$D/PrivateInclude -I04-Src/cmodel/eeg_filter \
  04-Src/cmodel/eeg_filter/eeg_filter.c 04-Src/cmodel/eeg_filter/tb_eeg_filter.c \
  $F/arm_biquad_cascade_df1_f32.c $F/arm_biquad_cascade_df1_init_f32.c -lm -o tb_eeg_filter
```

`__GNUC_PYTHON__` là công tắc của CMSIS-DSP để build trên host mà không cần header CMSIS-Core. Khi build cho Cortex-M4F, flow dùng header CMSIS-Core thật (`08-external/cmsis-core`, CMSIS 6 v6.1.0) vì CMSIS-DSP cần các intrinsic DSP (`__SMUAD`, `__QADD`, …). Trên firmware Zephyr/NCS, dùng CMSIS-DSP do module Zephyr cung cấp, không dùng công tắc này.
