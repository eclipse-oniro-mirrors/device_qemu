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

#include <cstdint>
#include <memory>
#include "buffer_handle.h"
#include "codec_jpeg_vdi.h"
#include "codec_log_wrapper.h"
#include "hdf_base.h"
#include "virt_jpeg_vdi_impl.h"

using namespace OHOS::VDI::JPEG;

namespace {
std::shared_ptr<VirtJpegImpl> g_virtJpegImpl = nullptr;

int32_t JpegInit()
{
    CODEC_LOGI("enter.");
    if (g_virtJpegImpl != nullptr) {
        CODEC_LOGE("virt jpeg impl is already inited.");
        return HDF_ERR_DEVICE_BUSY;
    }
    g_virtJpegImpl = std::make_shared<VirtJpegImpl>();
    return g_virtJpegImpl->Init();
}

int32_t JpegDeInit()
{
    CODEC_LOGI("enter.");
    if (g_virtJpegImpl != nullptr) {
        g_virtJpegImpl->DeInit();
    }
    g_virtJpegImpl = nullptr;
    return HDF_SUCCESS;
}

int32_t AllocateBuffer(BufferHandle **buffer, uint32_t size)
{
    if (g_virtJpegImpl == nullptr) {
        CODEC_LOGE("virt jpeg impl is not inited.");
        return HDF_ERR_NOPERM;
    }
    return g_virtJpegImpl->AllocateBuffer(buffer, size);
}

int32_t FreeBuffer(BufferHandle *buffer)
{
    if (g_virtJpegImpl == nullptr) {
        CODEC_LOGE("virt jpeg impl is not inited.");
        return HDF_ERR_NOPERM;
    }
    return g_virtJpegImpl->FreeBuffer(buffer);
}

int32_t DoJpegDecode(BufferHandle *buffer, BufferHandle *outBuffer, const struct CodecJpegDecInfo *decInfo)
{
    if (g_virtJpegImpl == nullptr || decInfo == nullptr) {
        CODEC_LOGE("virt jpeg impl is not inited or decInfo is null.");
        return HDF_ERR_NOPERM;
    }
    return g_virtJpegImpl->DeCode(buffer, outBuffer, *decInfo);
}

void NotifyPowerOn()
{
    /* software decode has no power rail to manage */
}
} // namespace

static ICodecJpegHwi g_jpegHwi = {.JpegInit = JpegInit,
                                  .JpegDeInit = JpegDeInit,
                                  .AllocateInBuffer = AllocateBuffer,
                                  .FreeInBuffer = FreeBuffer,
                                  .DoJpegDecode = DoJpegDecode,
                                  .NotifyPowerOn = NotifyPowerOn};

extern "C" ICodecJpegHwi *GetCodecJpegHwi()
{
    return &g_jpegHwi;
}
