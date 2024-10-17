#include "psxthread.h"

#include "mdec.h"
#include "video/psx.h"

#include <deque>
#include <condition_variable>

std::mutex psxthread_mutex;
std::mutex psxthread_deque_lock;

std::deque<psxthread_work*> m_psxthread_work;

psxmdec_device *psx_mdec = nullptr;
psxgpu_device *psx_gpu = nullptr;

std::condition_variable condition;

bool update_screen = true;
bitmap_rgb32 *screen_bitmap = nullptr;
rectangle screen_cliprect;

std::atomic_bool running;

void psxthread_start()
{
    running = true;

    while (running) {
        std::unique_lock<std::mutex> lock2{psxthread_deque_lock};

        while (running && m_psxthread_work.empty())
            condition.wait(lock2);

        if (!running)
            break;

        while (!m_psxthread_work.empty()) {
            psxthread_work *item = m_psxthread_work.front();
            m_psxthread_work.pop_front();

            if (item != nullptr) {
                switch (item->cmd) {
                    case PSXTHREAD_CMD::PSXMDEC_DMA_READ:
                        psx_mdec->dma_read_internal(item->payload.psxmdec_dma_read.p_n_psxram, item->payload.psxmdec_dma_read.n_address, item->payload.psxmdec_dma_read.n_size);
                        break;

                    case PSXTHREAD_CMD::PSXMDEC_DMA_WRITE:
                        psx_mdec->dma_write_internal(item->payload.psxmdec_dma_write.p_n_psxram, item->payload.psxmdec_dma_write.n_address, item->payload.psxmdec_dma_write.n_size);
                        break;

                    case PSXTHREAD_CMD::PSXMDEC_WRITE:
                        psx_mdec->write_internal(item->payload.psxmdec_write.offset, item->payload.psxmdec_write.data);
                        break;


                    case PSXTHREAD_CMD::PSXGPU_DMA_WRITE:
                        psx_gpu->gpu_write_internal(item->payload.psxgpu_dma_write.p_ram, item->payload.psxgpu_dma_write.n_size);
                        free(item->payload.psxgpu_dma_write.p_ram);
                        break;

                    case PSXTHREAD_CMD::PSXGPU_GPU_WRITE:
                        psx_gpu->gpu_write_internal(item->payload.psxgpu_gpu_write.p_ram, item->payload.psxgpu_gpu_write.n_size);
                        break;

                    case PSXTHREAD_CMD::PSXGPU_WRITE:
                        psx_gpu->write_internal(item->payload.psxgpu_write.offset, item->payload.psxgpu_write.data, item->payload.psxgpu_write.mem_mask);
                        break;

                    case PSXTHREAD_CMD::PSXGPU_UPDATE_SCREEN:
                        update_screen = true;
                        screen_bitmap = item->payload.psxgpu_update_screen.bitmap;
                        screen_cliprect = item->payload.psxgpu_update_screen.cliprect;
                        break;

                    default:
                        printf("Unhandled type: %d\n", item->cmd);
                        exit(1);
                        break;
                }

                free(item);
            }
        }

        if (screen_bitmap != nullptr && update_screen) {
            psx_gpu->update_screen_internal(*screen_bitmap, screen_cliprect);
            update_screen = false;
        }

        lock2.unlock();
    }
}

void psxthread_reset()
{
    auto lock = std::unique_lock<std::mutex>(psxthread_deque_lock);

    std::deque<psxthread_work*> empty;
    std::swap(m_psxthread_work, empty);

    lock.unlock();
}

void psxthread_lock()
{
    psxthread_mutex.lock();
}

void psxthread_unlock()
{
    psxthread_mutex.unlock();
}

void psxthread_addwork(psxthread_work *item)
{
    auto lock = std::unique_lock<std::mutex>(psxthread_deque_lock);

    m_psxthread_work.push_back(item);

    lock.unlock();
    condition.notify_one();
}

void psxthread_set_mdec(void *mdec)
{
    psx_mdec = (psxmdec_device*)mdec;
    psxthread_reset();
}

void psxthread_set_gpu(void *gpu)
{
    psx_gpu = (psxgpu_device*)gpu;
    psxthread_reset();
}

void psxthread_exit()
{
    running = false;
    condition.notify_one();
}