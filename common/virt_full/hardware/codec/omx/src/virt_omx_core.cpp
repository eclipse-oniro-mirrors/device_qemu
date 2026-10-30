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

#include <cstring>
#include <mutex>
#include <new>
#include <securec.h>

#include <OMX_Component.h>
#include <OMX_Core.h>
#include <OMX_Types.h>

#include "virt_omx_component.h"

namespace OHOS {
namespace VDI {
namespace OMX {

namespace {
struct ComponentDesc {
    const char *name;
    const char *role;
    bool encoder;
};

/*
 * The software component set of the virtual product. The names match the
 * entries of vendor/ohemu/virt/hdf_config/uhdf/media_codec/media_codec_capabilities.hcs
 * so the codec HDI manager can resolve every capability entry to this core.
 * The components implement the OpenMAX IL contract only; no bitstream content
 * processing happens inside them.
 */
const ComponentDesc COMPONENTS[] = {
    {"OMX.virt.video_encoder.avc", "video_encoder.avc", true},
    {"OMX.virt.video_encoder.hevc", "video_encoder.hevc", true},
    {"OMX.virt.video_decoder.avc", "video_decoder.avc", false},
    {"OMX.virt.video_decoder.m2v", "video_decoder.m2v", false},
    {"OMX.virt.video_decoder.vp8", "video_decoder.vp8", false},
    {"OMX.virt.video_decoder.h263", "video_decoder.h263", false},
    {"OMX.virt.video_decoder.m4v", "video_decoder.m4v", false},
    {"OMX.virt.video_decoder.flv1", "video_decoder.flv1", false},
    {"OMX.virt.video_decoder.mjpeg", "video_decoder.mjpeg", false},
    {"OMX.virt.video_decoder.hevc", "video_decoder.hevc", false},
    {"OMX.virt.video_decoder.vp9", "video_decoder.vp9", false},
};

constexpr uint32_t COMPONENT_COUNT = sizeof(COMPONENTS) / sizeof(COMPONENTS[0]);

std::mutex g_coreMutex;
uint32_t g_coreRefCount = 0;

const ComponentDesc *FindComponent(const char *name)
{
    if (name == nullptr) {
        return nullptr;
    }
    for (uint32_t i = 0; i < COMPONENT_COUNT; i++) {
        if (strcmp(COMPONENTS[i].name, name) == 0) {
            return &COMPONENTS[i];
        }
    }
    return nullptr;
}

const ComponentDesc *FindComponentByRole(const char *role)
{
    if (role == nullptr) {
        return nullptr;
    }
    for (uint32_t i = 0; i < COMPONENT_COUNT; i++) {
        if (strcmp(COMPONENTS[i].role, role) == 0) {
            return &COMPONENTS[i];
        }
    }
    return nullptr;
}

bool CopyRoleString(OMX_U8 *dst, const char *src)
{
    size_t len = strlen(src);
    if (len >= OMX_MAX_STRINGNAME_SIZE) {
        return false;
    }
    if (memset_s(dst, OMX_MAX_STRINGNAME_SIZE, 0, OMX_MAX_STRINGNAME_SIZE) != EOK) {
        return false;
    }
    return strncpy_s(reinterpret_cast<char *>(dst), OMX_MAX_STRINGNAME_SIZE, src, len) == EOK;
}
} // namespace

extern "C" {
/* OMX core life cycle, refcounted for the multiple dlopen sites */
OMX_ERRORTYPE OMX_Init(void)
{
    std::lock_guard<std::mutex> lk(g_coreMutex);
    g_coreRefCount++;
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMX_Deinit(void)
{
    std::lock_guard<std::mutex> lk(g_coreMutex);
    if (g_coreRefCount > 0) {
        g_coreRefCount--;
    }
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMX_GetHandle(OMX_HANDLETYPE *pHandle, OMX_STRING cComponentName, OMX_PTR pAppData,
    OMX_CALLBACKTYPE *pCallBacks)
{
    if (pHandle == nullptr || cComponentName == nullptr || pCallBacks == nullptr) {
        return OMX_ErrorBadParameter;
    }
    *pHandle = nullptr;
    const ComponentDesc *desc = FindComponent(cComponentName);
    if (desc == nullptr) {
        return OMX_ErrorInvalidComponent;
    }
    auto *component = new (std::nothrow) VirtOmxComponent(desc->name, desc->role, desc->encoder);
    if (component == nullptr) {
        return OMX_ErrorInsufficientResources;
    }
    if (!component->Init(pAppData, pCallBacks)) {
        delete component;
        return OMX_ErrorInvalidComponent;
    }
    *pHandle = component->ComponentHandle();
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMX_FreeHandle(OMX_HANDLETYPE hComponent)
{
    if (hComponent == nullptr) {
        return OMX_ErrorBadParameter;
    }
    auto *comp = static_cast<OMX_COMPONENTTYPE *>(hComponent);
    if (comp->pComponentPrivate == nullptr || comp->ComponentDeInit == nullptr) {
        return OMX_ErrorInvalidComponent;
    }
    OMX_ERRORTYPE err = comp->ComponentDeInit(hComponent);
    delete static_cast<VirtOmxComponent *>(comp->pComponentPrivate);
    return err;
}

OMX_ERRORTYPE OMX_ComponentNameEnum(OMX_STRING cComponentName, OMX_U32 nNameLength, OMX_U32 nIndex)
{
    if (cComponentName == nullptr || nNameLength == 0) {
        return OMX_ErrorBadParameter;
    }
    if (nIndex >= COMPONENT_COUNT) {
        return OMX_ErrorNoMore;
    }
    size_t len = strlen(COMPONENTS[nIndex].name);
    if (len >= nNameLength) {
        return OMX_ErrorBadParameter;
    }
    if (strncpy_s(cComponentName, nNameLength, COMPONENTS[nIndex].name, len) != EOK) {
        return OMX_ErrorBadParameter;
    }
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMX_GetRolesOfComponent(OMX_STRING compName, OMX_U32 *pNumRoles, OMX_U8 **roles)
{
    if (compName == nullptr || pNumRoles == nullptr) {
        return OMX_ErrorBadParameter;
    }
    const ComponentDesc *desc = FindComponent(compName);
    if (desc == nullptr) {
        return OMX_ErrorInvalidComponent;
    }
    if (roles == nullptr) {
        /* first call: report the number of roles */
        *pNumRoles = 1;
        return OMX_ErrorNone;
    }
    if (*pNumRoles < 1) {
        return OMX_ErrorBadParameter;
    }
    if (!CopyRoleString(roles[0], desc->role)) {
        return OMX_ErrorBadParameter;
    }
    *pNumRoles = 1;
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMX_GetComponentsOfRole(OMX_STRING role, OMX_U32 *pNumComps, OMX_U8 **compNames)
{
    if (role == nullptr || pNumComps == nullptr) {
        return OMX_ErrorBadParameter;
    }
    if (compNames == nullptr || *pNumComps < 1) {
        *pNumComps = 1;
        return OMX_ErrorNone;
    }
    const ComponentDesc *desc = FindComponentByRole(role);
    if (desc == nullptr) {
        *pNumComps = 0;
        return OMX_ErrorNone;
    }
    if (!CopyRoleString(compNames[0], desc->name)) {
        return OMX_ErrorBadParameter;
    }
    *pNumComps = 1;
    return OMX_ErrorNone;
}

} // extern "C"

} // namespace OMX
} // namespace VDI
} // namespace OHOS
