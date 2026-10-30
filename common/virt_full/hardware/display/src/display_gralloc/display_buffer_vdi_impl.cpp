/*
 * Copyright (c) 2023 Huawei Device Co., Ltd.
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

#include "display_buffer_vdi_impl.h"
#include <cerrno>
#include <cinttypes>
#include <unistd.h>
#include <sys/mman.h>
#include <securec.h>
#include "display_log.h"
#include "display_gralloc_gbm.h"
#include "display_gralloc_private.h"
#include "hdf_base.h"
#include "v1_0/display_composer_type.h"

namespace OHOS {
namespace HDI {
namespace DISPLAY {
using namespace OHOS::HDI::Display::Composer::V1_0;
using namespace OHOS::HDI::Display::Buffer::V1_2;

DisplayBufferVdiImpl::DisplayBufferVdiImpl()
{
#ifdef GRALLOC_GBM_SUPPORT
    int ret = GbmGrallocInitialize();
    if (ret != HDF_SUCCESS) {
        DISPLAY_LOGE("gbm construct failed");
    }
#endif
}

DisplayBufferVdiImpl::~DisplayBufferVdiImpl()
{
#ifdef GRALLOC_GBM_SUPPORT
    if (GbmGrallocUninitialize() != HDF_SUCCESS) {
        DISPLAY_LOGE("gbm distruct failed");
    }
#endif
}

int32_t DisplayBufferVdiImpl::AllocMem(const AllocInfo& info, BufferHandle*& handle) const
{
    return GbmAllocMem(&info, &handle);
}

void DisplayBufferVdiImpl::FreeMem(const BufferHandle& handle) const
{
    GbmFreeMem(const_cast<BufferHandle *>(&handle));
}

void* DisplayBufferVdiImpl::Mmap(const BufferHandle& handle) const
{
    return GbmMmap(const_cast<BufferHandle *>(&handle));
}

int32_t DisplayBufferVdiImpl::Unmap(const BufferHandle& handle) const
{
    return GbmUnmap(const_cast<BufferHandle *>(&handle));
}

int32_t DisplayBufferVdiImpl::FlushCache(const BufferHandle& handle) const
{
    return GbmFlushCache(const_cast<BufferHandle *>(&handle));
}

int32_t DisplayBufferVdiImpl::InvalidateCache(const BufferHandle& handle) const
{
    return GbmInvalidateCache(const_cast<BufferHandle *>(&handle));
}

int32_t DisplayBufferVdiImpl::ReAllocMem(const AllocInfo& info, const BufferHandle& inHandle,
    BufferHandle*& outHandle) const
{
    if (info.width == 0 || info.height == 0) {
        DISPLAY_LOGE("%{public}s: invalid geometry %{public}u x %{public}u", __func__, info.width, info.height);
        return HDF_ERR_INVALID_PARAM;
    }
    BufferHandle *newBuffer = nullptr;
    int32_t ret = GbmAllocMem(&info, &newBuffer);
    if (ret != DISPLAY_SUCCESS || newBuffer == nullptr) {
        DISPLAY_LOGE("%{public}s: alloc new buffer failed, ret %{public}d", __func__, ret);
        return ret;
    }
    // Preserve the contents of the old buffer up to the smaller size, like realloc(3).
    void *src = (inHandle.virAddr != nullptr) ? inHandle.virAddr
        : mmap(nullptr, inHandle.size, PROT_READ, MAP_SHARED, inHandle.fd, 0);
    if (src != nullptr && src != MAP_FAILED) {
        void *dst = mmap(nullptr, newBuffer->size, PROT_WRITE, MAP_SHARED, newBuffer->fd, 0);
        if (dst != nullptr && dst != MAP_FAILED) {
            size_t copySize = (inHandle.size < newBuffer->size) ? inHandle.size : newBuffer->size;
            if (memcpy_s(dst, newBuffer->size, src, copySize) != EOK) {
                DISPLAY_LOGW("%{public}s: copy contents partially failed", __func__);
            }
            munmap(dst, newBuffer->size);
        } else {
            DISPLAY_LOGW("%{public}s: map new buffer failed", __func__);
        }
        if (inHandle.virAddr == nullptr) {
            munmap(src, inHandle.size);
        }
    } else {
        DISPLAY_LOGW("%{public}s: map old buffer failed", __func__);
    }
    // The caller keeps ownership of inHandle (the test suite frees it explicitly
    // after the call); only the new buffer is handed out.
    outHandle = newBuffer;
    return DISPLAY_SUCCESS;
}

int32_t DisplayBufferVdiImpl::CloneDmaBufferHandle(const BufferHandle& inHandle, BufferHandle*& outHandle) const
{
    if (inHandle.fd < 0) {
        DISPLAY_LOGE("%{public}s: invalid fd %{public}d", __func__, inHandle.fd);
        return HDF_ERR_INVALID_PARAM;
    }
    int newFd = dup(inHandle.fd);
    if (newFd < 0) {
        DISPLAY_LOGE("%{public}s: dup failed errno %{public}d", __func__, errno);
        return HDF_FAILURE;
    }
    PriBufferHandle *clone = nullptr;
    do {
        clone = (PriBufferHandle *)malloc(sizeof(PriBufferHandle));
        if (clone == nullptr) {
            break;
        }
        errno_t eok = memset_s(clone, sizeof(PriBufferHandle), 0, sizeof(PriBufferHandle));
        if (eok != EOK) {
            break;
        }
        BufferHandle *dst = &clone->hdl;
        dst->fd = newFd;
        dst->width = inHandle.width;
        dst->height = inHandle.height;
        dst->stride = inHandle.stride;
        dst->size = inHandle.size;
        dst->usage = inHandle.usage;
        dst->format = inHandle.format;
        dst->reserveFds = 0;
        dst->reserveInts = 0;
        dst->virAddr = nullptr;
        outHandle = dst;
        return DISPLAY_SUCCESS;
    } while (0);
    if (clone != nullptr) {
        free(clone);
    }
    close(newFd);
    DISPLAY_LOGE("%{public}s: clone buffer handle failed", __func__);
    return HDF_FAILURE;
}

int32_t DisplayBufferVdiImpl::IsSupportedAlloc(const std::vector<VerifyAllocInfo>& infos,
    std::vector<bool>& supporteds) const
{
    return DISPLAY_NOT_SUPPORT;
}

int32_t DisplayBufferVdiImpl::RegisterBuffer(const BufferHandle& handle)
{
    DISPLAY_LOGE("%s is not supported", __func__);
    return DISPLAY_NOT_SUPPORT;
}

int32_t DisplayBufferVdiImpl::SetMetadata(const BufferHandle& handle, uint32_t key, const std::vector<uint8_t>& value)
{
    DISPLAY_LOGE("%s is not supported", __func__);
    return DISPLAY_NOT_SUPPORT;
}

int32_t DisplayBufferVdiImpl::GetMetadata(const BufferHandle& handle, uint32_t key, std::vector<uint8_t>& value)
{
    DISPLAY_LOGE("%s is not supported", __func__);
    return DISPLAY_NOT_SUPPORT;
}

int32_t DisplayBufferVdiImpl::ListMetadataKeys(const BufferHandle& handle, std::vector<uint32_t>& keys)
{
    DISPLAY_LOGE("%s is not supported", __func__);
    return DISPLAY_NOT_SUPPORT;
}

int32_t DisplayBufferVdiImpl::EraseMetadataKey(const BufferHandle& handle, uint32_t key)
{
    DISPLAY_LOGE("%s is not supported", __func__);
    return DISPLAY_NOT_SUPPORT;
}

int32_t DisplayBufferVdiImpl::GetImageLayout(const BufferHandle& handle, ImageLayout& layout) const
{
    DISPLAY_LOGE("%s is not supported", __func__);
    return DISPLAY_NOT_SUPPORT;
}

extern "C" IDisplayBufferVdi* CreateDisplayBufferVdi()
{
    return new DisplayBufferVdiImpl();
}

extern "C" void DestroyDisplayBufferVdi(IDisplayBufferVdi* vdi)
{
    delete vdi;
}
} // namespace DISPLAY
} // namespace HDI
} // namespace OHOS
