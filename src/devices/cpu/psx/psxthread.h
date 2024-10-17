#ifndef MAME_CPU_PSX_PSXTHREAD_H
#define MAME_CPU_PSX_PSXTHREAD_H

#pragma once

#include <mutex>

#include "emu.h"

enum PSXTHREAD_CMD : uint8_t {
    PSXMDEC_DMA_READ = 0,
    PSXMDEC_DMA_WRITE,
    PSXMDEC_WRITE,

    PSXGPU_GPU_WRITE,
    PSXGPU_DMA_WRITE,
    PSXGPU_WRITE,
    PSXGPU_UPDATE_SCREEN
};

typedef struct psxthread_work {
    PSXTHREAD_CMD cmd;

    union {
        struct {
            uint32_t *p_n_psxram;
            uint32_t n_address;
            int32_t n_size;
        } psxmdec_dma_read;

        struct {
            uint32_t *p_n_psxram;
            uint32_t n_address;
            int32_t n_size;
        } psxmdec_dma_write;

        struct {
            offs_t offset;
            uint32_t data;
        } psxmdec_write;

        struct {
            uint32_t *p_ram;
            int32_t n_size;
        } psxgpu_gpu_write;

        struct {
            uint32_t *p_ram;
            int32_t n_size;
        } psxgpu_dma_write;

        struct {
            offs_t offset;
            uint32_t data;
            uint32_t mem_mask;
        } psxgpu_write;

        struct {
            bitmap_rgb32 *bitmap;
            rectangle cliprect;
        } psxgpu_update_screen;
    } payload;
} psxthread_work;

void psxthread_start();
void psxthread_exit();

void psxthread_set_mdec(void *mdex);
void psxthread_set_gpu(void *gpu);

void psxthread_lock();
void psxthread_unlock();

void psxthread_addwork(psxthread_work *item);

#endif // MAME_CPU_PSX_PSXTHREAD_H