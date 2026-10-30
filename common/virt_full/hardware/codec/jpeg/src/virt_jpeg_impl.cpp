/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "virt_jpeg_vdi_impl.h"
#include <dlfcn.h>
#include "codec_log_wrapper.h"
#include "display_type.h"
#include "hdf_base.h"
#include "virt_jpeg_decoder.h"

namespace OHOS {
namespace VDI {
namespace JPEG {
using namespace OHOS::HDI::Display::Buffer::V1_0;

namespace {
constexpr uint32_t MAX_IN_BUFFER_SIZE = 50 * 1024 * 1024; // 50 MB, same as codec image service limit
constexpr uint32_t PIXEL_SIZE_RGBA8888 = 4;
constexpr uint32_t STRIDE_ALIGN = 16;
const char *DISPLAY_BUFFER_VDI_LIB = "libdisplay_buffer_vdi_impl.z.so";

uint32_t AlignUp(uint32_t value, uint32_t align)
{
    if (align == 0) {
        return value;
    }
    return (value + align - 1) / align * align;
}
} // namespace

int32_t VirtJpegImpl::LoadDisplayBufferVdi()
{
    displayLibHandle_ = dlopen(DISPLAY_BUFFER_VDI_LIB, RTLD_LAZY);
    if (displayLibHandle_ == nullptr) {
        CODEC_LOGE("dlopen %{public}s failed, error [%{public}s]", DISPLAY_BUFFER_VDI_LIB, dlerror());
        return HDF_FAILURE;
    }
    using CreateDisplayBufferVdi = IDisplayBufferVdi *(*)(void);
    auto createVdi = reinterpret_cast<CreateDisplayBufferVdi>(dlsym(displayLibHandle_, "CreateDisplayBufferVdi"));
    if (createVdi == nullptr) {
        CODEC_LOGE("dlsym CreateDisplayBufferVdi failed");
        dlclose(displayLibHandle_);
        displayLibHandle_ = nullptr;
        return HDF_FAILURE;
    }
    displayVdi_ = createVdi();
    if (displayVdi_ == nullptr) {
        CODEC_LOGE("CreateDisplayBufferVdi returned null");
        dlclose(displayLibHandle_);
        displayLibHandle_ = nullptr;
        return HDF_FAILURE;
    }
    CODEC_LOGI("load %{public}s success", DISPLAY_BUFFER_VDI_LIB);
    return HDF_SUCCESS;
}

int32_t VirtJpegImpl::Init()
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (inited_) {
        CODEC_LOGE("virt jpeg impl is already inited");
        return HDF_ERR_DEVICE_BUSY;
    }
    if (LoadDisplayBufferVdi() != HDF_SUCCESS) {
        return HDF_FAILURE;
    }
    if (VirtJpegDecoder::GetInstance().LoadLibjpeg() != HDF_SUCCESS) {
        CODEC_LOGE("load platform libjpeg-turbo failed, jpeg decode is unavailable");
        displayVdi_ = nullptr;
        dlclose(displayLibHandle_);
        displayLibHandle_ = nullptr;
        return HDF_FAILURE;
    }
    inited_ = true;
    CODEC_LOGI("virt jpeg impl init success");
    return HDF_SUCCESS;
}

void VirtJpegImpl::DeInit()
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (!inited_) {
        return;
    }
    displayVdi_ = nullptr;
    if (displayLibHandle_ != nullptr) {
        dlclose(displayLibHandle_);
        displayLibHandle_ = nullptr;
    }
    inited_ = false;
    CODEC_LOGI("virt jpeg impl deinit success");
}

int32_t VirtJpegImpl::AllocateBuffer(BufferHandle **buffer, uint32_t size)
{
    if (buffer == nullptr) {
        CODEC_LOGE("buffer ptr is null");
        return HDF_ERR_INVALID_PARAM;
    }
    if (size == 0 || size > MAX_IN_BUFFER_SIZE) {
        CODEC_LOGE("invalid buffer size %{public}u", size);
        return HDF_ERR_INVALID_PARAM;
    }
    if (displayVdi_ == nullptr) {
        CODEC_LOGE("display vdi is null, not inited");
        return HDF_ERR_NOPERM;
    }
    /*
     * The compressed jpeg data has no geometry, borrow an RGBA8888 allocation to
     * obtain one dma-buf of at least 'size' bytes:
     * memory = width * PIXEL_SIZE_RGBA8888 * height, take height as 2 rows and
     * width = AlignUp(size / (PIXEL_SIZE_RGBA8888 * height), STRIDE_ALIGN).
     */
    AllocInfo alloc = {
        .width = AlignUp(size / (PIXEL_SIZE_RGBA8888 * 2), STRIDE_ALIGN), // 2: rows of the allocation
        .height = 2,                                                      // 2: minimum rows
        .usage = HBM_USE_CPU_READ | HBM_USE_CPU_WRITE | HBM_USE_MEM_DMA,
        .format = PIXEL_FMT_RGBA_8888,
    };
    BufferHandle *handle = nullptr;
    int32_t ret = displayVdi_->AllocMem(alloc, handle);
    if (ret != HDF_SUCCESS) {
        CODEC_LOGE("AllocMem failed, ret %{public}d", ret);
        return ret;
    }
    if (handle == nullptr || static_cast<uint32_t>(handle->size) < size) {
        CODEC_LOGE("allocated buffer too small, size %{public}d, expect %{public}u",
            (handle == nullptr) ? -1 : handle->size, size);
        if (handle != nullptr) {
            displayVdi_->FreeMem(*handle);
        }
        return HDF_FAILURE;
    }
    *buffer = handle;
    return HDF_SUCCESS;
}

int32_t VirtJpegImpl::FreeBuffer(BufferHandle *buffer)
{
    if (buffer == nullptr) {
        CODEC_LOGE("buffer is null");
        return HDF_ERR_INVALID_PARAM;
    }
    if (displayVdi_ == nullptr) {
        CODEC_LOGE("display vdi is null, not inited");
        return HDF_ERR_NOPERM;
    }
    displayVdi_->FreeMem(*buffer);
    return HDF_SUCCESS;
}

int32_t VirtJpegImpl::DeCode(BufferHandle *inBuffer, BufferHandle *outBuffer, const struct CodecJpegDecInfo &decInfo)
{
    if (inBuffer == nullptr || outBuffer == nullptr) {
        CODEC_LOGE("inBuffer or outBuffer is null");
        return HDF_ERR_INVALID_PARAM;
    }
    if (displayVdi_ == nullptr) {
        CODEC_LOGE("display vdi is null, not inited");
        return HDF_ERR_NOPERM;
    }
    return VirtJpegDecoder::GetInstance().DeCode(displayVdi_, inBuffer, outBuffer, decInfo);
}
} // namespace JPEG
} // namespace VDI
} // namespace OHOS
