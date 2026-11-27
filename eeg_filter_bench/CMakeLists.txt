# eeg_filter_bench — EEG filter chain benchmark on the nRF52840 DK (task #17).
# Built by 05-Experimental/flow-fw-dsp-bench with nRF Connect SDK v2.6.1:
#   west build -b nrf52840dk_nrf52840 <this dir> -- -DEEG_GEN_DIR=<dir> -DEEG_FP_CONTRACT=off|fast
# EEG_GEN_DIR holds the generated cmsis_filter_coeffs.h and bench_vectors.h (never committed).
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(eeg_filter_bench)

set(EEG_CMODEL_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../cmodel/eeg_filter)
if(NOT DEFINED EEG_GEN_DIR)
  message(FATAL_ERROR "Set -DEEG_GEN_DIR=<directory with cmsis_filter_coeffs.h and bench_vectors.h>")
endif()
if(NOT DEFINED EEG_FP_CONTRACT)
  set(EEG_FP_CONTRACT off)
endif()

target_sources(app PRIVATE src/main.c ${EEG_CMODEL_DIR}/eeg_filter.c)
target_include_directories(app PRIVATE ${EEG_CMODEL_DIR} ${EEG_GEN_DIR})
# off: bit-exact with the MATLAB/host model; fast: GCC default (VFMA), realistic performance.
target_compile_options(app PRIVATE -ffp-contract=${EEG_FP_CONTRACT})
target_compile_definitions(app PRIVATE EEG_FP_CONTRACT_STR="${EEG_FP_CONTRACT}")
