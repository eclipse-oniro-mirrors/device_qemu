/*
 * Copyright (c) 2022 Unionman Co., Ltd.
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

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "display_gfx.h"
#include "securec.h"

static int32_t GetPixelBpp(PixelFormat fmt)
{
    switch (fmt) {
        case PIXEL_FMT_RGBX_8888:
        case PIXEL_FMT_RGBA_8888:
        case PIXEL_FMT_BGRX_8888:
        case PIXEL_FMT_BGRA_8888:
            return 4;
        case PIXEL_FMT_RGB_565:
        case PIXEL_FMT_BGR_565:
            return 2;
        default:
            return -1;
    }
}

/* Read a pixel and normalise it to 0xAABBGGRR (RGBA8888, R in the lowest byte). */
static uint32_t ReadPixelRgba(const uint8_t *base, int32_t stride, int32_t x, int32_t y, PixelFormat fmt)
{
    const uint8_t *px = base + (int64_t)y * stride + (int64_t)x * GetPixelBpp(fmt);
    switch (fmt) {
        case PIXEL_FMT_RGBA_8888:
            return (uint32_t)px[0] | ((uint32_t)px[1] << 8) | ((uint32_t)px[2] << 16) | ((uint32_t)px[3] << 24);
        case PIXEL_FMT_RGBX_8888:
            return (uint32_t)px[0] | ((uint32_t)px[1] << 8) | ((uint32_t)px[2] << 16) | 0xFF000000u;
        case PIXEL_FMT_BGRA_8888:
            return (uint32_t)px[2] | ((uint32_t)px[1] << 8) | ((uint32_t)px[0] << 16) | ((uint32_t)px[3] << 24);
        case PIXEL_FMT_BGRX_8888:
            return (uint32_t)px[2] | ((uint32_t)px[1] << 8) | ((uint32_t)px[0] << 16) | 0xFF000000u;
        case PIXEL_FMT_RGB_565: {
            uint16_t p;
            if (memcpy_s(&p, sizeof(p), px, sizeof(p)) != EOK) {
                return 0;
            }
            uint32_t r = (uint32_t)((p >> 11) & 0x1F) * 255 / 31;
            uint32_t g = (uint32_t)((p >> 5) & 0x3F) * 255 / 63;
            uint32_t b = (uint32_t)(p & 0x1F) * 255 / 31;
            return r | (g << 8) | (b << 16) | 0xFF000000u;
        }
        case PIXEL_FMT_BGR_565: {
            uint16_t p;
            if (memcpy_s(&p, sizeof(p), px, sizeof(p)) != EOK) {
                return 0;
            }
            uint32_t r = (uint32_t)(p & 0x1F) * 255 / 31;
            uint32_t g = (uint32_t)((p >> 5) & 0x3F) * 255 / 63;
            uint32_t b = (uint32_t)((p >> 11) & 0x1F) * 255 / 31;
            return r | (g << 8) | (b << 16) | 0xFF000000u;
        }
        default:
            return 0;
    }
}

/* Write a 0xAABBGGRR pixel to the surface honouring the surface format. */
static void WritePixelRgba(uint8_t *base, int32_t stride, int32_t x, int32_t y, PixelFormat fmt, uint32_t c)
{
    uint8_t *px = base + (int64_t)y * stride + (int64_t)x * GetPixelBpp(fmt);
    switch (fmt) {
        case PIXEL_FMT_RGBA_8888:
            px[0] = c & 0xFF;
            px[1] = (c >> 8) & 0xFF;
            px[2] = (c >> 16) & 0xFF;
            px[3] = (c >> 24) & 0xFF;
            break;
        case PIXEL_FMT_RGBX_8888:
            px[0] = c & 0xFF;
            px[1] = (c >> 8) & 0xFF;
            px[2] = (c >> 16) & 0xFF;
            px[3] = 0xFF;
            break;
        case PIXEL_FMT_BGRA_8888:
            px[0] = (c >> 16) & 0xFF;
            px[1] = (c >> 8) & 0xFF;
            px[2] = c & 0xFF;
            px[3] = (c >> 24) & 0xFF;
            break;
        case PIXEL_FMT_BGRX_8888:
            px[0] = (c >> 16) & 0xFF;
            px[1] = (c >> 8) & 0xFF;
            px[2] = c & 0xFF;
            px[3] = 0xFF;
            break;
        case PIXEL_FMT_RGB_565: {
            uint16_t p = (uint16_t)((((c >> 16) & 0xF8) << 8) | (((c >> 8) & 0xFC) << 3) | ((c & 0xF8) >> 3));
            (void)memcpy_s(px, sizeof(p), &p, sizeof(p));
            break;
        }
        case PIXEL_FMT_BGR_565: {
            uint16_t p = (uint16_t)(((c & 0xF8) << 8) | (((c >> 8) & 0xFC) << 3) | (((c >> 16) & 0xF8) >> 3));
            (void)memcpy_s(px, sizeof(p), &p, sizeof(p));
            break;
        }
        default:
            break;
    }
}

/* The virtual display passes the dma-buf fd through ISurface::phyAddr. */
static void *MapSurfaceMem(const ISurface *surface)
{
    if ((surface->phyAddr == 0) || (surface->stride <= 0) || (surface->height <= 0)) {
        return NULL;
    }
    size_t size = (size_t)surface->stride * (size_t)surface->height;
    void *addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, (int)surface->phyAddr, 0);
    return (addr == MAP_FAILED) ? NULL : addr;
}

static void UnmapSurfaceMem(const ISurface *surface, void *addr)
{
    if (addr != NULL) {
        munmap(addr, (size_t)surface->stride * (size_t)surface->height);
    }
}

static void ClampRect(IRect *rect, int32_t surfaceW, int32_t surfaceH)
{
    int32_t x0 = rect->x;
    int32_t y0 = rect->y;
    int32_t x1 = rect->x + rect->w;
    int32_t y1 = rect->y + rect->h;
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > surfaceW) {
        x1 = surfaceW;
    }
    if (y1 > surfaceH) {
        y1 = surfaceH;
    }
    rect->x = x0;
    rect->y = y0;
    rect->w = x1 - x0;
    rect->h = y1 - y0;
}

int32_t InitGfx()
{
    return DISPLAY_SUCCESS;
}

int32_t DeinitGfx()
{
    return DISPLAY_SUCCESS;
}

int32_t FillRect(ISurface *iSurface, IRect *rect, uint32_t color, GfxOpt *opt)
{
    (void)opt;
    if ((iSurface == NULL) || (rect == NULL)) {
        return DISPLAY_PARAM_ERR;
    }
    if (GetPixelBpp(iSurface->enColorFmt) <= 0) {
        return DISPLAY_NOT_SUPPORT;
    }
    IRect fillRect = *rect;
    ClampRect(&fillRect, iSurface->width, iSurface->height);
    if ((fillRect.w <= 0) || (fillRect.h <= 0)) {
        return DISPLAY_SUCCESS;
    }
    void *addr = MapSurfaceMem(iSurface);
    if (addr == NULL) {
        return DISPLAY_FAILURE;
    }
    for (int32_t y = fillRect.y; y < fillRect.y + fillRect.h; y++) {
        for (int32_t x = fillRect.x; x < fillRect.x + fillRect.w; x++) {
            WritePixelRgba((uint8_t *)addr, iSurface->stride, x, y, iSurface->enColorFmt, color);
        }
    }
    UnmapSurfaceMem(iSurface, addr);
    return DISPLAY_SUCCESS;
}

/* Straight-alpha SRC_OVER: out = src + dst * (1 - srcAlpha). */
static uint32_t BlendSrcOver(uint32_t src, uint32_t dst)
{
    uint32_t sa = (src >> 24) & 0xFF;
    if (sa == 0xFF) {
        return src;
    }
    if (sa == 0) {
        return dst;
    }
    uint32_t inv = 255 - sa;
    uint32_t a = sa + ((dst >> 24) & 0xFF) * inv / 255;
    if (a == 0) {
        return 0;
    }
    uint32_t outR = ((src & 0xFF) * sa + (dst & 0xFF) * inv) / a;
    uint32_t outG = (((src >> 8) & 0xFF) * sa + ((dst >> 8) & 0xFF) * inv) / a;
    uint32_t outB = (((src >> 16) & 0xFF) * sa + ((dst >> 16) & 0xFF) * inv) / a;
    return outR | (outG << 8) | (outB << 16) | (a << 24);
}

int32_t Blit(ISurface *srcSurface, IRect *srcRect, ISurface *dstSurface, IRect *dstRect, GfxOpt *opt)
{
    if ((srcSurface == NULL) || (srcRect == NULL) || (dstSurface == NULL) || (dstRect == NULL)) {
        return DISPLAY_PARAM_ERR;
    }
    if ((GetPixelBpp(srcSurface->enColorFmt) <= 0) || (GetPixelBpp(dstSurface->enColorFmt) <= 0)) {
        return DISPLAY_NOT_SUPPORT;
    }
    IRect srcR = *srcRect;
    IRect dstR = *dstRect;
    ClampRect(&srcR, srcSurface->width, srcSurface->height);
    ClampRect(&dstR, dstSurface->width, dstSurface->height);
    if ((srcR.w <= 0) || (srcR.h <= 0) || (dstR.w <= 0) || (dstR.h <= 0)) {
        return DISPLAY_SUCCESS;
    }

    void *srcAddr = MapSurfaceMem(srcSurface);
    void *dstAddr = MapSurfaceMem(dstSurface);
    if ((srcAddr == NULL) || (dstAddr == NULL)) {
        UnmapSurfaceMem(srcSurface, srcAddr);
        UnmapSurfaceMem(dstSurface, dstAddr);
        return DISPLAY_FAILURE;
    }

    BlendType blendType = (opt != NULL) ? opt->blendType : BLEND_NONE;
    bool usePixelAlpha = (opt != NULL) && opt->enPixelAlpha;
    /* The composition layer passes the layer global alpha via srcSurface->alpha0. */
    uint32_t globalAlpha = ((opt != NULL) && opt->enGlobalAlpha) ? srcSurface->alpha0 : 0xFF;

    for (int32_t dy = 0; dy < dstR.h; dy++) {
        int32_t sy = srcR.y + (int32_t)((int64_t)dy * srcR.h / dstR.h);
        for (int32_t dx = 0; dx < dstR.w; dx++) {
            int32_t sx = srcR.x + (int32_t)((int64_t)dx * srcR.w / dstR.w);
            uint32_t src = ReadPixelRgba((const uint8_t *)srcAddr, srcSurface->stride, sx, sy, srcSurface->enColorFmt);
            uint32_t sa = (src >> 24) & 0xFF;
            if (globalAlpha != 0xFF) {
                sa = sa * globalAlpha / 255;
                src = (src & 0x00FFFFFFu) | (sa << 24);
            }
            int32_t x = dstR.x + dx;
            int32_t y = dstR.y + dy;
            if ((blendType == BLEND_SRCOVER) && (usePixelAlpha || sa != 0xFF)) {
                uint32_t dst = ReadPixelRgba((const uint8_t *)dstAddr, dstSurface->stride, x, y,
                    dstSurface->enColorFmt);
                uint32_t out = BlendSrcOver(src, dst);
                WritePixelRgba((uint8_t *)dstAddr, dstSurface->stride, x, y, dstSurface->enColorFmt, out);
            } else if (blendType == BLEND_CLEAR) {
                WritePixelRgba((uint8_t *)dstAddr, dstSurface->stride, x, y, dstSurface->enColorFmt, 0);
            } else {
                /* BLEND_NONE / BLEND_SRC and the remaining modes replace the target. */
                WritePixelRgba((uint8_t *)dstAddr, dstSurface->stride, x, y, dstSurface->enColorFmt, src);
            }
        }
    }
    UnmapSurfaceMem(srcSurface, srcAddr);
    UnmapSurfaceMem(dstSurface, dstAddr);
    return DISPLAY_SUCCESS;
}

int32_t Sync(int32_t timeOut)
{
    (void)timeOut;
    /* The CPU implementation is synchronous, nothing to wait for. */
    return DISPLAY_SUCCESS;
}

int32_t GfxInitialize(GfxFuncs **funcs)
{
    if (funcs == NULL) {
        return DISPLAY_PARAM_ERR;
    }
    GfxFuncs *gfxFuncs = (GfxFuncs *)malloc(sizeof(GfxFuncs));
    if (gfxFuncs == NULL) {
        return DISPLAY_NOMEM;
    }

    errno_t eok = memset_s((void *)gfxFuncs, sizeof(GfxFuncs), 0, sizeof(GfxFuncs));
    if (eok != EOK) {
        free(gfxFuncs);
        return DISPLAY_FAILURE;
    }
    gfxFuncs->InitGfx = InitGfx;
    gfxFuncs->DeinitGfx = DeinitGfx;
    gfxFuncs->FillRect = FillRect;
    gfxFuncs->Blit = Blit;
    gfxFuncs->Sync = Sync;
    *funcs = gfxFuncs;

    return DISPLAY_SUCCESS;
}

int32_t GfxUninitialize(GfxFuncs *funcs)
{
    free(funcs);
    return DISPLAY_SUCCESS;
}
