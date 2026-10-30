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

#ifndef VIRT_JPEG_DECODER_H
#define VIRT_JPEG_DECODER_H

#include <csetjmp>
#include <cstdint>
#include <mutex>
#include <vector>
#include "buffer_handle.h"
#include "codec_jpeg_vdi.h"
#include "idisplay_buffer_vdi.h"
#include "jerror.h"
#include "jpeglib.h"

namespace OHOS {
namespace VDI {
namespace JPEG {
/*
 * Software jpeg decoder used by the virtual product jpeg VDI.
 *
 * The HDI caller feeds the entropy-coded scan data only and passes the parsed
 * jpeg header (quant/huffman tables, frame geometry) through CodecJpegDecInfo,
 * mirroring what a hardware decoder receives. The software backend rebuilds a
 * complete jpeg stream from these inputs and decodes it with the platform
 * libjpeg-turbo, writing the result into the caller supplied output buffer.
 */
class VirtJpegDecoder {
public:
    static VirtJpegDecoder &GetInstance()
    {
        static VirtJpegDecoder instance;
        return instance;
    }

    /* dlopen the platform libjpeg-turbo shared library (libjpeg API). */
    int32_t LoadLibjpeg();

    int32_t DeCode(OHOS::HDI::Display::Buffer::V1_0::IDisplayBufferVdi *displayVdi,
        BufferHandle *inBuffer, BufferHandle *outBuffer, const struct CodecJpegDecInfo &decInfo);

private:
    VirtJpegDecoder() = default;
    ~VirtJpegDecoder();

    int32_t AssembleJpegStream(const uint8_t *scanData, uint32_t scanLen,
        const struct CodecJpegDecInfo &decInfo, std::vector<uint8_t> &stream);
    int32_t DecodeToBuffer(OHOS::HDI::Display::Buffer::V1_0::IDisplayBufferVdi *displayVdi,
        const uint8_t *stream, uint32_t streamLen, BufferHandle *outBuffer,
        const struct CodecJpegDecInfo &decInfo);

    void *libHandle_ = nullptr;
    std::mutex mutex_;
};

/* jpeg api function types resolved from the shared library. */
using JpegCreateDecompressFn = void (*)(j_decompress_ptr, int, size_t);
using JpegMemSrcFn = void (*)(j_decompress_ptr, const unsigned char *, unsigned long);
using JpegReadHeaderFn = int (*)(j_decompress_ptr, boolean);
using JpegStartDecompressFn = boolean (*)(j_decompress_ptr);
using JpegReadScanlinesFn = JDIMENSION (*)(j_decompress_ptr, JSAMPARRAY, JDIMENSION);
using JpegFinishDecompressFn = boolean (*)(j_decompress_ptr);
using JpegDestroyDecompressFn = void (*)(j_decompress_ptr);
using JpegStdErrorFn = struct jpeg_error_mgr *(*)(struct jpeg_error_mgr *);

struct JpegLibApi {
    JpegCreateDecompressFn createDecompress = nullptr;
    JpegMemSrcFn memSrc = nullptr;
    JpegReadHeaderFn readHeader = nullptr;
    JpegStartDecompressFn startDecompress = nullptr;
    JpegReadScanlinesFn readScanlines = nullptr;
    JpegFinishDecompressFn finishDecompress = nullptr;
    JpegDestroyDecompressFn destroyDecompress = nullptr;
    JpegStdErrorFn stdError = nullptr;
};

struct VirtJpegErrorMgr {
    struct jpeg_error_mgr pub;
    std::jmp_buf jmpBuf;
};
} // namespace JPEG
} // namespace VDI
} // namespace OHOS

#endif // VIRT_JPEG_DECODER_H
