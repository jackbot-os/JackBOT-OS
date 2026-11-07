#ifndef JACKBOT_GPU_NV_H
#define JACKBOT_GPU_NV_H

#include <stdint.h>
#include <kernel/gpu.h>

/*
 * NVIDIA GPU driver, stage 1: identification through the PMC block.
 * Register offsets and bitfields come from the public envytools
 * documentation (docs/gpu/nvidia-stage0.md). Read-only on hardware.
 */

int gpu_nv_attach(struct gpu_dev *g);
int gpu_nv_present(void);
uint32_t gpu_nv_gpuid(void);
const char *gpu_nv_name(void);

#endif
