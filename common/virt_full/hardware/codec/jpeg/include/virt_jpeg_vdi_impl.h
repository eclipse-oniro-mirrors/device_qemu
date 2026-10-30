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

#ifndef VIRT_JPEG_VDI_IMPL_H
#define VIRT_JPEG_VDI_IMPL_H

#include <cstdint>
#include <memory>
#include <mutex>
#include "buffer_handle.h"
#include "codec_jpeg_vdi.h"
#include "idisplay_buffer_vdi.h"

namespace OHOS {
namespace VDI {
namespace JPEG {
/*
 * Software JPEG decode backend for the x86_64 virtual product.
 * Buffers are allocated through the display buffer VDI (real dma-buf memory),
 * decoding is performed in software by the platform libjpeg-turbo library.
 */
class VirtJpegImpl {
public:
    VirtJpegImpl() = default;
    ~VirtJpegImpl() = default;

    int32_t Init();
    void DeInit();
    int32_t AllocateBuffer(BufferHandle **buffer, uint32_t size);
    int32_t FreeBuffer(BufferHandle *buffer);
    int32_t DeCode(BufferHandle *inBuffer, BufferHandle *outBuffer, const struct CodecJpegDecInfo &decInfo);

private:
    int32_t LoadDisplayBufferVdi();
    bool inited_ = false;
    std::mutex mutex_;
    void *displayLibHandle_ = nullptr;
    OHOS::HDI::Display::Buffer::V1_0::IDisplayBufferVdi *displayVdi_ = nullptr;
};
} // namespace JPEG
} // namespace VDI
} // namespace OHOS

#endif // VIRT_JPEG_VDI_IMPL_H
