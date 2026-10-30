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

#include "virt_jpeg_decoder.h"
#include <cinttypes>
#include <cstring>
#include <cstdlib>
#include <dlfcn.h>
#include "codec_log_wrapper.h"
#include "display_type.h"
#include "hdf_base.h"
#include "securec.h"

namespace OHOS {
namespace VDI {
namespace JPEG {
using namespace OHOS::HDI::Display::Buffer::V1_0;

namespace {
/* candidate library paths, tried in order */
const char * const LIBJPEG_PATHS[] = {
    "libturbojpeg.z.so",
    "/system/lib64/chipset-sdk/libturbojpeg.z.so",
    "/system/lib64/platformsdk/libturbojpeg.z.so",
};

/* jpeg markers */
constexpr uint8_t MARKER_FF = 0xFF;
constexpr uint8_t MARKER_SOI = 0xD8;
constexpr uint8_t MARKER_EOI = 0xD9;
constexpr uint8_t MARKER_DQT = 0xDB;
constexpr uint8_t MARKER_SOF0 = 0xC0;
constexpr uint8_t MARKER_DHT = 0xC4;
constexpr uint8_t MARKER_DRI = 0xDD;
constexpr uint8_t MARKER_SOS = 0xDA;

constexpr uint32_t HUFF_BITS_LEN = 16;
constexpr uint32_t QUANT_TABLE_LEN = 64;
constexpr uint32_t MAX_JPEG_DIMENSION = 65535;
constexpr uint32_t JPEG_MAX_COMPONENTS = 3; /* YCbCr only, CMYK is out of scope of this HDI */
constexpr uint32_t PACKED_YCBCR_STRIDE = 3; /* [Y Cb Cr] triplets when out_color_space is JCS_YCbCr */
constexpr uint32_t UV_NEUTRAL = 0x80;
constexpr uint8_t SPECTRAL_SELECTION_END = 63; /* Se: a baseline scan covers all 64 DCT coefficients */
constexpr uint32_t UV_HEIGHT_DIVISOR = 2; /* 4:2:0 semi-planar: UV plane height is half of the Y plane */

/* api table resolved once by LoadLibjpeg, used by all decode calls */
JpegLibApi g_libapi;

struct StreamWriter {
    std::vector<uint8_t> &out;
    explicit StreamWriter(std::vector<uint8_t> &o) : out(o) {}
    void U8(uint8_t v)
    {
        out.push_back(v);
    }
    void U16(uint16_t v) /* big endian */
    {
        out.push_back(static_cast<uint8_t>(v >> 8)); // 8: high byte shift
        out.push_back(static_cast<uint8_t>(v & 0xFF));
    }
    void Marker(uint8_t m)
    {
        U8(MARKER_FF);
        U8(m);
    }
    void Bytes(const uint8_t *data, size_t len)
    {
        out.insert(out.end(), data, data + len);
    }
};

void JpegErrorExit(j_common_ptr cinfo)
{
    auto *err = reinterpret_cast<VirtJpegErrorMgr *>(cinfo->err);
    char msg[JMSG_LENGTH_MAX] = {0};
    (*cinfo->err->format_message)(cinfo, msg);
    CODEC_LOGE("libjpeg error: %{public}s", msg);
    longjmp(err->jmpBuf, 1);
}

bool GetOutGeometry(const struct CodecJpegDecInfo &decInfo, uint32_t &outW, uint32_t &outH)
{
    uint32_t w = decInfo.imageWidth;
    uint32_t h = decInfo.imageHeight;
    if (decInfo.region.flag != 0) {
        /* crop rect: [left, right) x [top, bottom) */
        uint32_t l = decInfo.region.left;
        uint32_t r = decInfo.region.right;
        uint32_t t = decInfo.region.top;
        uint32_t b = decInfo.region.bottom;
        if (r <= l || b <= t || r > w || b > h) {
            CODEC_LOGE("invalid crop region l:%{public}u r:%{public}u t:%{public}u b:%{public}u", l, r, t, b);
            return false;
        }
        w = r - l;
        h = b - t;
    }
    outW = w;
    outH = h;
    return true;
}

uint32_t PixelSizeOfFormat(int32_t format)
{
    switch (format) {
        case PIXEL_FMT_RGB_888:
            return 3; // 3: bytes per pixel
        case PIXEL_FMT_BGR_565:
            return 2; // 2: bytes per pixel
        default:
            return 0; /* yuv formats are handled separately */
    }
}

/* ---------- helpers for AssembleJpegStream ---------- */

int32_t CheckJpegStreamParams(const struct CodecJpegDecInfo &decInfo)
{
    if (decInfo.dataPrecision != 8) { // 8: the platform libjpeg is built for 8-bit samples
        CODEC_LOGE("unsupported data precision %{public}u", decInfo.dataPrecision);
        return HDF_ERR_NOT_SUPPORT;
    }
    if (decInfo.progressiveMode) {
        /* progressive streams consist of multiple scans whose per-scan headers are
         * not transported by CodecJpegDecInfo, a software backend can not rebuild
         * them from this contract. */
        CODEC_LOGE("progressive jpeg is not supported");
        return HDF_ERR_NOT_SUPPORT;
    }
    if (decInfo.arithCode) {
        CODEC_LOGE("arithmetic coding is not supported");
        return HDF_ERR_NOT_SUPPORT;
    }
    if (decInfo.numComponents == 0 || decInfo.numComponents > JPEG_MAX_COMPONENTS ||
        decInfo.compInfo.size() != decInfo.numComponents) {
        CODEC_LOGE("invalid numComponents %{public}u", decInfo.numComponents);
        return HDF_ERR_NOT_SUPPORT;
    }
    if (decInfo.imageWidth == 0 || decInfo.imageHeight == 0 ||
        decInfo.imageWidth > MAX_JPEG_DIMENSION || decInfo.imageHeight > MAX_JPEG_DIMENSION) {
        CODEC_LOGE("invalid image geometry %{public}u x %{public}u", decInfo.imageWidth, decInfo.imageHeight);
        return HDF_ERR_INVALID_PARAM;
    }
    return HDF_SUCCESS;
}

int32_t WriteQuantTables(StreamWriter &w, const struct CodecJpegDecInfo &decInfo)
{
    /* DQT: quantization tables */
    for (uint32_t idx = 0; idx < decInfo.quantTbl.size(); idx++) {
        const auto &tbl = decInfo.quantTbl[idx];
        if (!tbl.tableFlag) {
            continue;
        }
        if (tbl.quantVal.size() != QUANT_TABLE_LEN) {
            CODEC_LOGE("quant table %{public}u has invalid size %{public}zu", idx, tbl.quantVal.size());
            return HDF_ERR_INVALID_PARAM;
        }
        w.Marker(MARKER_DQT);
        w.U16(2 + 1 + QUANT_TABLE_LEN); // 2: length field, 1: Pq/Tq byte, 64: entries
        w.U8(static_cast<uint8_t>(idx)); /* Pq=0: 8-bit entries */
        for (uint32_t i = 0; i < QUANT_TABLE_LEN; i++) {
            w.U8(static_cast<uint8_t>(tbl.quantVal[i] & 0xFF)); // 8: low byte
        }
    }
    return HDF_SUCCESS;
}

int32_t CheckComponentTables(const struct CodecJpegDecInfo &decInfo)
{
    /* every component must reference present tables */
    for (uint32_t i = 0; i < decInfo.numComponents; i++) {
        const auto &comp = decInfo.compInfo[i];
        if (comp.hSampFactor == 0 || comp.hSampFactor > 4 || comp.vSampFactor == 0 || // 4: jpeg sampling limit
            comp.vSampFactor > 4) {
            CODEC_LOGE("component %{public}u invalid sampling %{public}u x %{public}u",
                i, comp.hSampFactor, comp.vSampFactor);
            return HDF_ERR_INVALID_PARAM;
        }
        if (comp.quantTableNo >= decInfo.quantTbl.size() || !decInfo.quantTbl[comp.quantTableNo].tableFlag) {
            CODEC_LOGE("component %{public}u misses quant table %{public}u", i, comp.quantTableNo);
            return HDF_ERR_INVALID_PARAM;
        }
        if (comp.dcTableNo >= decInfo.dcHuffTbl.size() || !decInfo.dcHuffTbl[comp.dcTableNo].tableFlag) {
            CODEC_LOGE("component %{public}u misses dc huff table %{public}u", i, comp.dcTableNo);
            return HDF_ERR_INVALID_PARAM;
        }
        if (comp.acTableNo >= decInfo.acHuffTbl.size() || !decInfo.acHuffTbl[comp.acTableNo].tableFlag) {
            CODEC_LOGE("component %{public}u misses ac huff table %{public}u", i, comp.acTableNo);
            return HDF_ERR_INVALID_PARAM;
        }
    }
    return HDF_SUCCESS;
}

void WriteSofHeader(StreamWriter &w, const struct CodecJpegDecInfo &decInfo)
{
    /* SOF0: baseline frame header */
    w.Marker(MARKER_SOF0);
    w.U16(8 + 3 * decInfo.numComponents); // 8: fixed bytes, 3: bytes per component
    w.U8(static_cast<uint8_t>(decInfo.dataPrecision));
    w.U16(static_cast<uint16_t>(decInfo.imageHeight));
    w.U16(static_cast<uint16_t>(decInfo.imageWidth));
    w.U8(static_cast<uint8_t>(decInfo.numComponents));
    for (uint32_t i = 0; i < decInfo.numComponents; i++) {
        const auto &comp = decInfo.compInfo[i];
        w.U8(static_cast<uint8_t>(comp.componentId));
        w.U8(static_cast<uint8_t>((comp.hSampFactor << 4) | comp.vSampFactor)); // 4: high nibble
        w.U8(static_cast<uint8_t>(comp.quantTableNo));
    }
}

int32_t WriteOneHuffTable(StreamWriter &w, const CodecJpegHuffTable &tbl, uint8_t tc, uint8_t th)
{
    if (tbl.bits.size() != HUFF_BITS_LEN) {
        CODEC_LOGE("huff table %{public}u bits size %{public}zu invalid", th, tbl.bits.size());
        return HDF_ERR_INVALID_PARAM;
    }
    w.Marker(MARKER_DHT);
    w.U16(static_cast<uint16_t>(2 + 1 + HUFF_BITS_LEN + tbl.huffVal.size())); // 2: length, 1: TcTh, 16: counts
    w.U8(static_cast<uint8_t>((tc << 4) | th)); // 4: high nibble
    w.Bytes(tbl.bits.data(), HUFF_BITS_LEN);
    if (!tbl.huffVal.empty()) {
        w.Bytes(tbl.huffVal.data(), tbl.huffVal.size());
    }
    return HDF_SUCCESS;
}

int32_t WriteHuffTables(StreamWriter &w, const struct CodecJpegDecInfo &decInfo)
{
    /* DHT: huffman tables */
    for (uint32_t idx = 0; idx < decInfo.dcHuffTbl.size(); idx++) {
        if (decInfo.dcHuffTbl[idx].tableFlag) {
            int32_t ret = WriteOneHuffTable(w, decInfo.dcHuffTbl[idx], 0, static_cast<uint8_t>(idx)); // 0: DC class
            if (ret != HDF_SUCCESS) {
                return ret;
            }
        }
    }
    for (uint32_t idx = 0; idx < decInfo.acHuffTbl.size(); idx++) {
        if (decInfo.acHuffTbl[idx].tableFlag) {
            int32_t ret = WriteOneHuffTable(w, decInfo.acHuffTbl[idx], 1, static_cast<uint8_t>(idx)); // 1: AC class
            if (ret != HDF_SUCCESS) {
                return ret;
            }
        }
    }
    return HDF_SUCCESS;
}

void WriteScanHeader(StreamWriter &w, const struct CodecJpegDecInfo &decInfo)
{
    /* SOS: scan header, baseline full band */
    w.Marker(MARKER_SOS);
    w.U16(static_cast<uint16_t>(6 + 2 * decInfo.numComponents)); // 6: fixed bytes, 2: bytes per component
    w.U8(static_cast<uint8_t>(decInfo.numComponents));
    for (uint32_t i = 0; i < decInfo.numComponents; i++) {
        const auto &comp = decInfo.compInfo[i];
        w.U8(static_cast<uint8_t>(comp.componentId));
        w.U8(static_cast<uint8_t>((comp.dcTableNo << 4) | comp.acTableNo)); // 4: high nibble
    }
    w.U8(0);  /* Ss */
    w.U8(SPECTRAL_SELECTION_END); /* Se */
    w.U8(0);  /* Ah/Al */
}

/* ---------- helpers for DecodeToBuffer ---------- */

int32_t CheckDecodeRequest(BufferHandle *outBuffer, const struct CodecJpegDecInfo &decInfo,
    bool &isYuv420Sp, uint32_t &outW, uint32_t &outH, uint32_t &stride, uint32_t &allocH)
{
    if (!GetOutGeometry(decInfo, outW, outH)) {
        return HDF_ERR_INVALID_PARAM;
    }
    isYuv420Sp = (outBuffer->format == PIXEL_FMT_YCBCR_420_SP ||
        outBuffer->format == PIXEL_FMT_YCRCB_420_SP);
    if (!isYuv420Sp && PixelSizeOfFormat(outBuffer->format) == 0) {
        CODEC_LOGE("unsupported output format %{public}d", outBuffer->format);
        return HDF_ERR_NOT_SUPPORT;
    }
    /* output capacity check, y plane occupies stride * allocation height, the uv
     * plane of semi-planar yuv follows directly after it (linear layout). */
    stride = static_cast<uint32_t>(outBuffer->stride);
    if (stride == 0) {
        CODEC_LOGE("output buffer stride is 0");
        return HDF_ERR_INVALID_PARAM;
    }
    allocH = (outBuffer->height > 0) ? static_cast<uint32_t>(outBuffer->height) : outH;
    uint64_t needSize = 0;
    if (isYuv420Sp) {
        if (outH > allocH) {
            CODEC_LOGE("output height %{public}u exceeds allocation %{public}u", outH, allocH);
            return HDF_ERR_INVALID_PARAM;
        }
        needSize = static_cast<uint64_t>(stride) * allocH +                          /* Y plane */
                   static_cast<uint64_t>(stride) * ((outH + 1) / UV_HEIGHT_DIVISOR); /* UV plane */
    } else {
        needSize = static_cast<uint64_t>(stride) * outH;
    }
    if (needSize > static_cast<uint64_t>(outBuffer->size)) {
        CODEC_LOGE("output buffer too small, need %{public}" PRIu64 ", have %{public}d",
            needSize, outBuffer->size);
        return HDF_ERR_INVALID_PARAM;
    }
    return HDF_SUCCESS;
}

int32_t MapOutputAndPrevRow(IDisplayBufferVdi *displayVdi, BufferHandle *outBuffer,
    bool needPrevRow, uint32_t prevRowSize, void **outVa, uint8_t **prevRow)
{
    *outVa = displayVdi->Mmap(*outBuffer);
    if (*outVa == nullptr) {
        CODEC_LOGE("mmap output buffer failed");
        return HDF_FAILURE;
    }
    *prevRow = nullptr;
    if (!needPrevRow) {
        return HDF_SUCCESS;
    }
    constexpr uint32_t maxPrevRowBytes = 4 * 1024 * 1024; /* one scanline never exceeds this */
    if (prevRowSize == 0 || prevRowSize > maxPrevRowBytes) {
        CODEC_LOGE("invalid prev row size %{public}u", prevRowSize);
        displayVdi->Unmap(*outBuffer);
        return HDF_ERR_INVALID_PARAM;
    }
    *prevRow = static_cast<uint8_t *>(malloc(prevRowSize));
    if (*prevRow == nullptr) {
        CODEC_LOGE("alloc prev row buffer failed");
        displayVdi->Unmap(*outBuffer);
        return HDF_FAILURE;
    }
    return HDF_SUCCESS;
}

bool SetupJpegDecode(struct jpeg_decompress_struct *cinfo, JSAMPARRAY *rowBuf, uint32_t *rowBytes,
    const uint8_t *stream, uint32_t streamLen, const struct CodecJpegDecInfo &decInfo,
    int32_t outFormat, bool isYuv420Sp)
{
    g_libapi.createDecompress(cinfo, JPEG_LIB_VERSION, sizeof(struct jpeg_decompress_struct));
    g_libapi.memSrc(cinfo, stream, streamLen);
    (void)g_libapi.readHeader(cinfo, TRUE);
    if (cinfo->image_width != decInfo.imageWidth || cinfo->image_height != decInfo.imageHeight) {
        CODEC_LOGE("stream geometry mismatch: %{public}u x %{public}u, expect %{public}u x %{public}u",
            cinfo->image_width, cinfo->image_height, decInfo.imageWidth, decInfo.imageHeight);
        return false;
    }
    if (static_cast<uint32_t>(cinfo->num_components) != decInfo.numComponents) {
        CODEC_LOGE("stream components %{public}d, expect %{public}u",
            cinfo->num_components, decInfo.numComponents);
        return false;
    }
    const bool grayInput = (cinfo->num_components == 1);
    if (isYuv420Sp) {
        cinfo->out_color_space = grayInput ? JCS_GRAYSCALE : JCS_YCbCr;
    } else if (outFormat == PIXEL_FMT_RGB_888) {
        cinfo->out_color_space = JCS_EXT_RGB;
    } else { /* PIXEL_FMT_BGR_565 */
        cinfo->out_color_space = JCS_RGB565;
    }
    g_libapi.startDecompress(cinfo);
    *rowBytes = cinfo->output_width * cinfo->output_components;
    *rowBuf = (*cinfo->mem->alloc_sarray)(reinterpret_cast<j_common_ptr>(cinfo), JPOOL_IMAGE, *rowBytes, 1);
    if (*rowBuf == nullptr) {
        CODEC_LOGE("alloc scanline buffer failed");
        return false;
    }
    return true;
}

/* row writing context, filled once before the scanline loop */
struct DecodeRowCtx {
    uint32_t outW;
    uint32_t outH;
    uint32_t stride;
    uint32_t cropLeft;
    uint32_t cropTop;
    bool swapChroma;
    bool grayscale;
    bool needPrevRow;
    uint32_t prevRowSize;
    uint32_t rowBytes;
    uint8_t *yPlane;
    uint8_t *uvPlane;
    uint8_t *prevRow;
};

void WriteYuvLumaRow(const DecodeRowCtx &ctx, const uint8_t *src, uint32_t dstRow)
{
    /* packed YCbCr rows: [Y Cb Cr] triplets, grayscale rows: [Y] */
    uint8_t *yRow = ctx.yPlane + static_cast<uint64_t>(ctx.stride) * dstRow;
    if (ctx.grayscale) {
        for (uint32_t x = 0; x < ctx.outW; x++) {
            yRow[x] = src[ctx.cropLeft + x];
        }
    } else {
        for (uint32_t x = 0; x < ctx.outW; x++) {
            yRow[x] = src[(ctx.cropLeft + x) * PACKED_YCBCR_STRIDE];
        }
    }
}

void WriteYuvChromaRow(const DecodeRowCtx &ctx, const uint8_t *src, uint32_t dstRow)
{
    /* chroma is written once per row pair; for odd heights the last
     * (half) chroma row is written by the final image row */
    uint8_t *uvRow = ctx.uvPlane + static_cast<uint64_t>(ctx.stride) * (dstRow / 2); // 2: chroma row
    uint32_t chromaPairs = (ctx.outW + 1) / 2; // 2: pair of pixels per chroma sample
    if (ctx.grayscale) {
        for (uint32_t x = 0; x < chromaPairs; x++) {
            uvRow[2 * x] = UV_NEUTRAL;     // 2: interleaved uv
            uvRow[2 * x + 1] = UV_NEUTRAL; // 2: interleaved uv
        }
        return;
    }
    const uint8_t *cur = src;
    const uint8_t *prv = (dstRow > 0) ? ctx.prevRow : cur;
    for (uint32_t x = 0; x < chromaPairs; x++) {
        uint32_t px = ctx.cropLeft / 2 + x; // 2: chroma sample covers 2 pixels
        uint32_t col = px * 2;              // 2: left pixel of the pair
        uint8_t cb = static_cast<uint8_t>(
            (cur[col * PACKED_YCBCR_STRIDE + 1] + prv[col * PACKED_YCBCR_STRIDE + 1]) / 2); // 2: avg
        uint8_t cr = static_cast<uint8_t>(
            (cur[col * PACKED_YCBCR_STRIDE + 2] + prv[col * PACKED_YCBCR_STRIDE + 2]) / 2); // 2: avg
        uvRow[2 * x] = ctx.swapChroma ? cr : cb;     // 2: interleaved uv
        uvRow[2 * x + 1] = ctx.swapChroma ? cb : cr; // 2: interleaved uv
    }
}

int32_t WriteYuvRow(const DecodeRowCtx &ctx, const uint8_t *src, uint32_t dstRow)
{
    WriteYuvLumaRow(ctx, src, dstRow);
    bool writeChroma = ((dstRow & 1) == 1 && dstRow + 1 < ctx.outH) || (dstRow + 1 == ctx.outH);
    if (writeChroma) {
        WriteYuvChromaRow(ctx, src, dstRow);
    }
    if (ctx.needPrevRow && memcpy_s(ctx.prevRow, ctx.prevRowSize, src, ctx.rowBytes) != EOK) {
        CODEC_LOGE("copy prev row failed");
        return HDF_FAILURE;
    }
    return HDF_SUCCESS;
}

int32_t WriteRgbRow(const DecodeRowCtx &ctx, const uint8_t *src, uint32_t dstRow)
{
    uint8_t *dst = ctx.yPlane + static_cast<uint64_t>(ctx.stride) * dstRow;
    if (memcpy_s(dst, ctx.stride, src + ctx.cropLeft * 3, ctx.outW * 3) != EOK) { // 3: bytes per pixel
        CODEC_LOGE("copy rgb row failed");
        return HDF_FAILURE;
    }
    return HDF_SUCCESS;
}

void WriteBgr565Row(const DecodeRowCtx &ctx, const uint8_t *src, uint32_t dstRow)
{
    /* PIXEL_FMT_BGR_565: byte swapped RGB565 */
    uint16_t *dst = reinterpret_cast<uint16_t *>(ctx.yPlane + static_cast<uint64_t>(ctx.stride) * dstRow);
    const uint16_t *s = reinterpret_cast<const uint16_t *>(src + ctx.cropLeft * 2); // 2: b/px
    for (uint32_t x = 0; x < ctx.outW; x++) {
        dst[x] = static_cast<uint16_t>((s[x] >> 8) | (s[x] << 8)); // 8: byte swap
    }
}

int32_t WriteDecodedRow(const DecodeRowCtx &ctx, const uint8_t *src, uint32_t srcRow,
    bool isYuv420Sp, bool isRgb888)
{
    uint32_t dstRow = srcRow - ctx.cropTop;
    if (isYuv420Sp) {
        return WriteYuvRow(ctx, src, dstRow);
    }
    if (isRgb888) {
        return WriteRgbRow(ctx, src, dstRow);
    }
    WriteBgr565Row(ctx, src, dstRow);
    return HDF_SUCCESS;
}

int32_t DecodeRows(struct jpeg_decompress_struct *cinfo, JSAMPARRAY rowBuf, uint32_t rowBytes,
    void *outVa, int32_t outFormat, bool isYuv420Sp, uint32_t outW, uint32_t outH, uint32_t stride,
    uint32_t allocH, const struct CodecJpegDecInfo &decInfo, bool needPrevRow, uint8_t *prevRow,
    uint32_t prevRowSize)
{
    if (needPrevRow && rowBytes > prevRowSize) {
        CODEC_LOGE("scanline size %{public}u exceeds prev row %{public}u", rowBytes, prevRowSize);
        return HDF_FAILURE;
    }
    /* crop window inside the decoded image */
    const uint32_t cropLeft = (decInfo.region.flag != 0) ? decInfo.region.left : 0;
    const uint32_t cropTop = (decInfo.region.flag != 0) ? decInfo.region.top : 0;
    uint8_t *yPlane = static_cast<uint8_t *>(outVa);
    uint8_t *uvPlane = yPlane + static_cast<uint64_t>(stride) * allocH;
    DecodeRowCtx ctx = {
        outW, outH, stride, cropLeft, cropTop,
        (outFormat == PIXEL_FMT_YCRCB_420_SP), (cinfo->out_color_space == JCS_GRAYSCALE), needPrevRow,
        prevRowSize, rowBytes, yPlane, uvPlane, prevRow,
    };
    const bool isRgb888 = (outFormat == PIXEL_FMT_RGB_888);
    uint32_t writtenRows = 0;
    while (cinfo->output_scanline < cinfo->output_height) {
        uint32_t srcRow = cinfo->output_scanline;
        (void)g_libapi.readScanlines(cinfo, rowBuf, 1);
        if (srcRow < cropTop || srcRow >= cropTop + outH) {
            continue;
        }
        if (WriteDecodedRow(ctx, rowBuf[0], srcRow, isYuv420Sp, isRgb888) != HDF_SUCCESS) {
            return HDF_FAILURE;
        }
        writtenRows++;
    }
    if (writtenRows != outH) {
        CODEC_LOGE("decoded rows %{public}u, expect %{public}u", writtenRows, outH);
        return HDF_FAILURE;
    }
    return HDF_SUCCESS;
}

int32_t FinishDecode(IDisplayBufferVdi *displayVdi, BufferHandle *outBuffer,
    struct jpeg_decompress_struct *cinfo, uint8_t *prevRow, bool decodeOk)
{
    int32_t ret = decodeOk ? HDF_SUCCESS : HDF_FAILURE;
    if (decodeOk) {
        (void)g_libapi.finishDecompress(cinfo);
    }
    g_libapi.destroyDecompress(cinfo);
    if (prevRow != nullptr) {
        free(prevRow);
    }
    displayVdi->Unmap(*outBuffer);
    if (ret == HDF_SUCCESS) {
        displayVdi->FlushCache(*outBuffer);
    }
    return ret;
}

} // namespace

VirtJpegDecoder::~VirtJpegDecoder()
{
    if (libHandle_ != nullptr) {
        dlclose(libHandle_);
        libHandle_ = nullptr;
    }
}

int32_t VirtJpegDecoder::LoadLibjpeg()
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (libHandle_ != nullptr) {
        return HDF_SUCCESS;
    }
    for (const char *path : LIBJPEG_PATHS) {
        void *handle = dlopen(path, RTLD_LAZY);
        if (handle == nullptr) {
            CODEC_LOGW("dlopen %{public}s failed, try next", path);
            continue;
        }
        JpegLibApi api = {};
        api.createDecompress = reinterpret_cast<JpegCreateDecompressFn>(dlsym(handle, "jpeg_CreateDecompress"));
        api.memSrc = reinterpret_cast<JpegMemSrcFn>(dlsym(handle, "jpeg_mem_src"));
        api.readHeader = reinterpret_cast<JpegReadHeaderFn>(dlsym(handle, "jpeg_read_header"));
        api.startDecompress = reinterpret_cast<JpegStartDecompressFn>(dlsym(handle, "jpeg_start_decompress"));
        api.readScanlines = reinterpret_cast<JpegReadScanlinesFn>(dlsym(handle, "jpeg_read_scanlines"));
        api.finishDecompress = reinterpret_cast<JpegFinishDecompressFn>(dlsym(handle, "jpeg_finish_decompress"));
        api.destroyDecompress = reinterpret_cast<JpegDestroyDecompressFn>(dlsym(handle, "jpeg_destroy_decompress"));
        api.stdError = reinterpret_cast<JpegStdErrorFn>(dlsym(handle, "jpeg_std_error"));
        if (api.createDecompress == nullptr || api.memSrc == nullptr || api.readHeader == nullptr ||
            api.startDecompress == nullptr || api.readScanlines == nullptr || api.finishDecompress == nullptr ||
            api.destroyDecompress == nullptr || api.stdError == nullptr) {
            CODEC_LOGE("dlsym jpeg api incomplete for %{public}s", path);
            dlclose(handle);
            continue;
        }
        g_libapi = api;
        libHandle_ = handle; /* kept open, the api table references its code */
        CODEC_LOGI("load platform libjpeg from %{public}s success", path);
        return HDF_SUCCESS;
    }
    CODEC_LOGE("no usable platform libjpeg library found");
    return HDF_FAILURE;
}

int32_t VirtJpegDecoder::AssembleJpegStream(const uint8_t *scanData, uint32_t scanLen,
    const struct CodecJpegDecInfo &decInfo, std::vector<uint8_t> &stream)
{
    int32_t ret = CheckJpegStreamParams(decInfo);
    if (ret != HDF_SUCCESS) {
        return ret;
    }

    StreamWriter w(stream);
    w.Marker(MARKER_SOI);

    ret = WriteQuantTables(w, decInfo);
    if (ret != HDF_SUCCESS) {
        return ret;
    }
    ret = CheckComponentTables(decInfo);
    if (ret != HDF_SUCCESS) {
        return ret;
    }
    WriteSofHeader(w, decInfo);

    ret = WriteHuffTables(w, decInfo);
    if (ret != HDF_SUCCESS) {
        return ret;
    }
    if (decInfo.restartInterval != 0) {
        /* DRI: restart interval */
        w.Marker(MARKER_DRI);
        w.U16(4); // 4: length
        w.U16(static_cast<uint16_t>(decInfo.restartInterval));
    }

    WriteScanHeader(w, decInfo);
    /* entropy coded data followed by EOI */
    w.Bytes(scanData, scanLen);
    w.Marker(MARKER_EOI);
    return HDF_SUCCESS;
}

int32_t VirtJpegDecoder::DecodeToBuffer(IDisplayBufferVdi *displayVdi, const uint8_t *stream,
    uint32_t streamLen, BufferHandle *outBuffer, const struct CodecJpegDecInfo &decInfo)
{
    if (libHandle_ == nullptr || g_libapi.createDecompress == nullptr) {
        CODEC_LOGE("libjpeg api is not loaded");
        return HDF_FAILURE;
    }
    uint32_t outW = 0;
    uint32_t outH = 0;
    uint32_t stride = 0;
    uint32_t allocH = 0;
    bool isYuv420Sp = false;
    int32_t ret = CheckDecodeRequest(outBuffer, decInfo, isYuv420Sp, outW, outH, stride, allocH);
    if (ret != HDF_SUCCESS) {
        return ret;
    }

    /*
     * The previous scanline is cached to average the chroma of the two rows of
     * each 2x2 block. It is allocated BEFORE setjmp with a size derived from
     * decInfo, so its value stays valid after a longjmp (values assigned after
     * setjmp would be indeterminate there).
     */
    const bool needPrevRow = isYuv420Sp && decInfo.numComponents == JPEG_MAX_COMPONENTS;
    const uint32_t prevRowSize = static_cast<uint32_t>(decInfo.imageWidth) * PACKED_YCBCR_STRIDE;
    void *outVa = nullptr;
    uint8_t *prevRow = nullptr;
    ret = MapOutputAndPrevRow(displayVdi, outBuffer, needPrevRow, prevRowSize, &outVa, &prevRow);
    if (ret != HDF_SUCCESS) {
        return ret;
    }

    struct jpeg_decompress_struct cinfo;
    /* zeroed cinfo keeps jpeg_destroy_decompress safe even if a fatal error
     * happens before the struct is fully initialized */
    (void)memset_s(&cinfo, sizeof(cinfo), 0, sizeof(cinfo));
    VirtJpegErrorMgr jerr;
    cinfo.err = g_libapi.stdError(&jerr.pub);
    JSAMPARRAY rowBuf = nullptr;
    uint32_t rowBytes = 0;
    bool decodeOk = false;

    if (setjmp(jerr.jmpBuf) == 0) {
        jerr.pub.error_exit = JpegErrorExit;
        if (!SetupJpegDecode(&cinfo, &rowBuf, &rowBytes, stream, streamLen, decInfo,
            outBuffer->format, isYuv420Sp)) {
            goto OUT;
        }
        if (DecodeRows(&cinfo, rowBuf, rowBytes, outVa, outBuffer->format, isYuv420Sp, outW, outH,
            stride, allocH, decInfo, needPrevRow, prevRow, prevRowSize) != HDF_SUCCESS) {
            goto OUT;
        }
        decodeOk = true;
    } else {
        /* longjmp landed here: libjpeg reported a fatal error */
        CODEC_LOGE("jpeg decode aborted by libjpeg");
    }

OUT:
    return FinishDecode(displayVdi, outBuffer, &cinfo, prevRow, decodeOk);
}

int32_t VirtJpegDecoder::DeCode(IDisplayBufferVdi *displayVdi, BufferHandle *inBuffer,
    BufferHandle *outBuffer, const struct CodecJpegDecInfo &decInfo)
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (libHandle_ == nullptr) {
        CODEC_LOGE("libjpeg is not loaded");
        return HDF_FAILURE;
    }
    if (displayVdi == nullptr || inBuffer == nullptr || outBuffer == nullptr) {
        return HDF_ERR_INVALID_PARAM;
    }
    if (decInfo.compressPos >= static_cast<uint32_t>(inBuffer->size)) {
        CODEC_LOGE("compressPos %{public}u out of range, size %{public}d",
            decInfo.compressPos, inBuffer->size);
        return HDF_ERR_INVALID_PARAM;
    }
    void *inVa = displayVdi->Mmap(*inBuffer);
    if (inVa == nullptr) {
        CODEC_LOGE("mmap input buffer failed");
        return HDF_FAILURE;
    }
    const uint8_t *scanData = static_cast<uint8_t *>(inVa) + decInfo.compressPos;
    const uint32_t scanLen = static_cast<uint32_t>(inBuffer->size) - decInfo.compressPos;

    std::vector<uint8_t> stream;
    stream.reserve(scanLen + 1024); // 1024: headroom for the rebuilt headers
    int32_t ret = AssembleJpegStream(scanData, scanLen, decInfo, stream);
    displayVdi->Unmap(*inBuffer);
    if (ret != HDF_SUCCESS) {
        return ret;
    }
    return DecodeToBuffer(displayVdi, stream.data(), static_cast<uint32_t>(stream.size()), outBuffer, decInfo);
}
} // namespace JPEG
} // namespace VDI
} // namespace OHOS
