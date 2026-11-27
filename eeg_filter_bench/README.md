# eeg_filter_bench — đo chuỗi lọc EEG trên nRF52840 DK

Task #17. Ứng dụng Zephyr (nRF Connect SDK v2.6.1, board `nrf52840dk_nrf52840`) chạy đúng mã `04-Src/cmodel/eeg_filter/eeg_filter.c` trên Cortex-M4F 64 MHz.

Ứng dụng làm hai việc:
- **Kiểm tra đúng:** chạy 1 kênh trên vector golden của `flow-dsp-filter`, so khớp bit với đầu ra float32 của MATLAB và tính sai số so với tham chiếu float64.
- **Đo chu kỳ:** chạy 8 kênh với khối 1/5/25/50/250 mẫu, đếm chu kỳ bằng bộ đếm DWT CYCCNT, khóa ngắt trong lúc đo.

Không chạy tay: build, nạp, đọc UART và tổng hợp đều do `05-Experimental/flow-fw-dsp-bench` thực hiện (README của flow).

| File | Nội dung |
|---|---|
| `CMakeLists.txt` | Thêm `eeg_filter.c`. Nhận `-DEEG_GEN_DIR=<thư mục header sinh ra>` và `-DEEG_FP_CONTRACT=off\|fast` |
| `prj.conf` | `CONFIG_FPU`, `CONFIG_CMSIS_DSP`, `CONFIG_CMSIS_DSP_FILTERING`, console UART |
| `src/main.c` | Benchmark; in các dòng `RESULT ...` và `BENCH done` qua console 115200 baud |

Hai header `cmsis_filter_coeffs.h` và `bench_vectors.h` do flow sinh ra trong thư mục `run_*/`. Chúng không vào git.

`-ffp-contract=off` cho kết quả khớp bit với mô hình MATLAB và bản C trên host. `fast` là mặc định của GCC: trình biên dịch gộp nhân–cộng thành lệnh VFMA, nên phản ánh hiệu năng thực tế; khi đó chỉ so sai số theo ngưỡng F4.

Trạng thái 2026-11-10: mã đã viết nhưng **chưa build và chưa chạy**, vì máy phát triển chưa cài NCS v2.6.1.
