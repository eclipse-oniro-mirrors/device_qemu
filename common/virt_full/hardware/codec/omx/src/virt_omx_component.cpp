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

#include "virt_omx_component.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <securec.h>

#include <OMX_CoreExt.h>
#include <OMX_Index.h>
#include <OMX_VideoExt.h>

namespace OHOS {
namespace VDI {
namespace OMX {

namespace {
/* spec version the component claims to implement: OpenMAX IL 1.1.2 */
constexpr OMX_U32 OMX_SPEC_VERSION = 0x01010000;
/* component version: 1.0 */
constexpr OMX_U32 VIRT_COMPONENT_VERSION = 0x01000000;
/* default encoded stream buffer size for compressed ports */
constexpr OMX_U32 DEFAULT_COMPRESSED_BUFFER_SIZE = 1024 * 1024;
/* raw NV12 frame size for the default port geometry (640x480, 4:2:0) */
constexpr OMX_U32 DEFAULT_RAW_BUFFER_SIZE = (640 * 480 * 3) / 2;
/* buffer types the component works with through the codec HDI buffer wrappers */
constexpr uint32_t SUPPORTED_BUFFER_TYPES =
    CODEC_BUFFER_TYPE_AVSHARE_MEM_FD | CODEC_BUFFER_TYPE_HANDLE | CODEC_BUFFER_TYPE_DYNAMIC_HANDLE |
    CODEC_BUFFER_TYPE_DMA_MEM_FD;

OMX_VIDEO_CODINGTYPE CodingOfRole(const std::string &role)
{
    if (role.find("avc") != std::string::npos) {
        return OMX_VIDEO_CodingAVC;
    }
    if (role.find("m2v") != std::string::npos) {
        return OMX_VIDEO_CodingMPEG2;
    }
    if (role.find("vp8") != std::string::npos) {
        /* OMX_VIDEO_CodingVP8 lives in the extension enum, cast to the base type */
        return static_cast<OMX_VIDEO_CODINGTYPE>(OMX_VIDEO_CodingVP8);
    }
    if (role.find("h263") != std::string::npos) {
        return OMX_VIDEO_CodingH263;
    }
    if (role.find("m4v") != std::string::npos) {
        return OMX_VIDEO_CodingMPEG4;
    }
    if (role.find("mjpeg") != std::string::npos) {
        return OMX_VIDEO_CodingMJPEG;
    }
    /* hevc/vp9/flv1 have no OMX IL 1.1.2 coding value, keep them unused */
    return OMX_VIDEO_CodingUnused;
}

/* vtable thunks */
OMX_ERRORTYPE GetComponentVersionCb(OMX_HANDLETYPE h, OMX_STRING n, OMX_VERSIONTYPE *cv,
    OMX_VERSIONTYPE *sv, OMX_UUIDTYPE *u)
{
    return VirtOmxComponent::Self(h)->GetComponentVersion(n, cv, sv, u);
}

OMX_ERRORTYPE SendCommandCb(OMX_HANDLETYPE h, OMX_COMMANDTYPE c, OMX_U32 p1, OMX_PTR p2)
{
    return VirtOmxComponent::Self(h)->SendCommand(c, p1, p2);
}

OMX_ERRORTYPE GetParameterCb(OMX_HANDLETYPE h, OMX_INDEXTYPE i, OMX_PTR p)
{
    return VirtOmxComponent::Self(h)->GetParameter(i, p);
}

OMX_ERRORTYPE SetParameterCb(OMX_HANDLETYPE h, OMX_INDEXTYPE i, OMX_PTR p)
{
    return VirtOmxComponent::Self(h)->SetParameter(i, p);
}

OMX_ERRORTYPE GetConfigCb(OMX_HANDLETYPE h, OMX_INDEXTYPE i, OMX_PTR p)
{
    return VirtOmxComponent::Self(h)->GetConfig(i, p);
}

OMX_ERRORTYPE SetConfigCb(OMX_HANDLETYPE h, OMX_INDEXTYPE i, OMX_PTR p)
{
    return VirtOmxComponent::Self(h)->SetConfig(i, p);
}

OMX_ERRORTYPE GetExtensionIndexCb(OMX_HANDLETYPE h, OMX_STRING n, OMX_INDEXTYPE *i)
{
    return VirtOmxComponent::Self(h)->GetExtensionIndex(n, i);
}

OMX_ERRORTYPE GetStateCb(OMX_HANDLETYPE h, OMX_STATETYPE *s)
{
    return VirtOmxComponent::Self(h)->GetState(s);
}

OMX_ERRORTYPE ComponentTunnelRequestCb(OMX_HANDLETYPE h, OMX_U32 port, OMX_HANDLETYPE tc,
    OMX_U32 tp, OMX_TUNNELSETUPTYPE *ts)
{
    return VirtOmxComponent::Self(h)->ComponentTunnelRequest(port, tc, tp, ts);
}

OMX_ERRORTYPE UseBufferCb(OMX_HANDLETYPE h, OMX_BUFFERHEADERTYPE **bh, OMX_U32 port, OMX_PTR ap,
    OMX_U32 size, OMX_U8 *buf)
{
    return VirtOmxComponent::Self(h)->UseBuffer(bh, port, ap, size, buf);
}

OMX_ERRORTYPE AllocateBufferCb(OMX_HANDLETYPE h, OMX_BUFFERHEADERTYPE **bh, OMX_U32 port, OMX_PTR ap,
    OMX_U32 size)
{
    return VirtOmxComponent::Self(h)->AllocateBuffer(bh, port, ap, size);
}

OMX_ERRORTYPE FreeBufferCb(OMX_HANDLETYPE h, OMX_U32 port, OMX_BUFFERHEADERTYPE *b)
{
    return VirtOmxComponent::Self(h)->FreeBuffer(port, b);
}

OMX_ERRORTYPE EmptyThisBufferCb(OMX_HANDLETYPE h, OMX_BUFFERHEADERTYPE *b)
{
    return VirtOmxComponent::Self(h)->EmptyThisBuffer(b);
}

OMX_ERRORTYPE FillThisBufferCb(OMX_HANDLETYPE h, OMX_BUFFERHEADERTYPE *b)
{
    return VirtOmxComponent::Self(h)->FillThisBuffer(b);
}

OMX_ERRORTYPE SetCallbacksCb(OMX_HANDLETYPE h, OMX_CALLBACKTYPE *cb, OMX_PTR ap)
{
    return VirtOmxComponent::Self(h)->SetCallbacks(cb, ap);
}

OMX_ERRORTYPE ComponentDeInitCb(OMX_HANDLETYPE h)
{
    return VirtOmxComponent::Self(h)->ComponentDeInit();
}

OMX_ERRORTYPE UseEglImageCb(OMX_HANDLETYPE h, OMX_BUFFERHEADERTYPE **bh, OMX_U32 port, OMX_PTR ap,
    OMX_PTR egl)
{
    return VirtOmxComponent::Self(h)->UseEglImage(bh, port, ap, egl);
}

OMX_ERRORTYPE ComponentRoleEnumCb(OMX_HANDLETYPE h, OMX_U8 *role, OMX_U32 index)
{
    return VirtOmxComponent::Self(h)->ComponentRoleEnum(role, index);
}

/* raw frame MIME for the default video ports */
constexpr char RAW_VIDEO_MIME[] = "video/raw";
/* PIXEL_FMT_YCBCR_420_SP from display_type.h (the codec ext API reports
 * color formats with the display pixel format values) */
constexpr uint32_t PIXEL_FMT_YCBCR_420_SP_VALUE = 28;

bool SizeMatches(OMX_PTR structPtr, OMX_U32 expected)
{
    if (structPtr == nullptr) {
        return false;
    }
    return *(static_cast<const OMX_U32 *>(structPtr)) == expected;
}
} // namespace

VirtOmxComponent::VirtOmxComponent(const std::string &name, const std::string &role, bool isEncoder)
    : name_(name), role_(role), isEncoder_(isEncoder), state_(OMX_StateLoaded), hasPendingState_(false),
      pendingState_(OMX_StateLoaded), appData_(nullptr), lowLatency_(false), workerRunning_(false)
{
    (void)memset_s(&comp_, sizeof(comp_), 0, sizeof(comp_));
    (void)memset_s(&callbacks_, sizeof(callbacks_), 0, sizeof(callbacks_));
    (void)memset_s(&bitrateParam_, sizeof(bitrateParam_), 0, sizeof(bitrateParam_));
    (void)memset_s(&bitrateConfig_, sizeof(bitrateConfig_), 0, sizeof(bitrateConfig_));
    (void)memset_s(&overlayParam_, sizeof(overlayParam_), 0, sizeof(overlayParam_));
}

VirtOmxComponent::~VirtOmxComponent()
{
    StopWorker();
    for (uint32_t port = 0; port < PORT_COUNT; port++) {
        for (BufferEntry *entry : ports_[port].buffers) {
            DeleteEntry(entry);
        }
        ports_[port].buffers.clear();
    }
}

bool VirtOmxComponent::Init(OMX_PTR appData, const OMX_CALLBACKTYPE *callbacks)
{
    if (callbacks == nullptr) {
        return false;
    }
    if (callbacks->EventHandler == nullptr || callbacks->EmptyBufferDone == nullptr ||
        callbacks->FillBufferDone == nullptr) {
        return false;
    }

    appData_ = appData;
    callbacks_ = *callbacks;

    comp_.nSize = sizeof(OMX_COMPONENTTYPE);
    comp_.nVersion.nVersion = OMX_SPEC_VERSION;
    comp_.pComponentPrivate = this;
    comp_.pApplicationPrivate = appData;
    comp_.GetComponentVersion = GetComponentVersionCb;
    comp_.SendCommand = SendCommandCb;
    comp_.GetParameter = GetParameterCb;
    comp_.SetParameter = SetParameterCb;
    comp_.GetConfig = GetConfigCb;
    comp_.SetConfig = SetConfigCb;
    comp_.GetExtensionIndex = GetExtensionIndexCb;
    comp_.GetState = GetStateCb;
    comp_.ComponentTunnelRequest = ComponentTunnelRequestCb;
    comp_.UseBuffer = UseBufferCb;
    comp_.AllocateBuffer = AllocateBufferCb;
    comp_.FreeBuffer = FreeBufferCb;
    comp_.EmptyThisBuffer = EmptyThisBufferCb;
    comp_.FillThisBuffer = FillThisBufferCb;
    comp_.SetCallbacks = SetCallbacksCb;
    comp_.ComponentDeInit = ComponentDeInitCb;
    comp_.UseEGLImage = UseEglImageCb;
    comp_.ComponentRoleEnum = ComponentRoleEnumCb;

    InitPorts();

    workerRunning_ = true;
    worker_ = std::thread(&VirtOmxComponent::WorkerLoop, this);
    return true;
}

OMX_COMPONENTTYPE *VirtOmxComponent::ComponentHandle()
{
    return &comp_;
}

VirtOmxComponent *VirtOmxComponent::Self(OMX_HANDLETYPE hComponent)
{
    OMX_COMPONENTTYPE *comp = static_cast<OMX_COMPONENTTYPE *>(hComponent);
    return static_cast<VirtOmxComponent *>(comp->pComponentPrivate);
}

VirtOmxComponent::BufferEntry *VirtOmxComponent::EntryOf(OMX_BUFFERHEADERTYPE *buffer)
{
    /* hdr is the first member of BufferEntry */
    return reinterpret_cast<BufferEntry *>(buffer);
}

void VirtOmxComponent::InitPorts()
{
    const OMX_VIDEO_CODINGTYPE coding = CodingOfRole(role_);
    for (uint32_t i = 0; i < PORT_COUNT; i++) {
        VirtPort &port = ports_[i];
        (void)memset_s(&port.def, sizeof(port.def), 0, sizeof(port.def));
        port.useBufferType = CODEC_BUFFER_TYPE_AVSHARE_MEM_FD;
        port.def.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
        port.def.nVersion.nVersion = OMX_SPEC_VERSION;
        port.def.nPortIndex = i;
        port.def.bEnabled = OMX_TRUE;
        port.def.bPopulated = OMX_FALSE;
        port.def.eDomain = OMX_PortDomainVideo;
        port.def.eDir = (i == PORT_INPUT) ? OMX_DirInput : OMX_DirOutput;
        port.def.bBuffersContiguous = OMX_FALSE;
        port.def.nBufferAlignment = 0;
        port.def.nBufferCountActual = DEFAULT_BUFFER_COUNT;
        port.def.nBufferCountMin = 1;
        port.def.nBufferSize = (i == PORT_INPUT && !isEncoder_) ? DEFAULT_COMPRESSED_BUFFER_SIZE
                                                               : DEFAULT_RAW_BUFFER_SIZE;
        port.def.format.video.cMIMEType = const_cast<char *>(RAW_VIDEO_MIME);
        port.def.format.video.pNativeRender = nullptr;
        port.def.format.video.nFrameWidth = DEFAULT_RAW_WIDTH;
        port.def.format.video.nFrameHeight = DEFAULT_RAW_HEIGHT;
        port.def.format.video.nStride = DEFAULT_RAW_WIDTH;
        port.def.format.video.nSliceHeight = DEFAULT_RAW_HEIGHT;
        port.def.format.video.xFramerate = DEFAULT_FRAMERATE_Q16;
        port.def.format.video.bFlagErrorConcealment = OMX_FALSE;
        if (i == PORT_INPUT) {
            /* input of an encoder carries raw frames, input of a decoder carries the stream */
            port.def.format.video.eCompressionFormat =
                isEncoder_ ? OMX_VIDEO_CodingUnused : coding;
            port.def.format.video.eColorFormat =
                isEncoder_ ? OMX_COLOR_FormatYUV420SemiPlanar : OMX_COLOR_FormatUnused;
        } else {
            /* output of an encoder carries the stream, output of a decoder carries raw frames */
            port.def.format.video.eCompressionFormat = isEncoder_ ? coding : OMX_VIDEO_CodingUnused;
            port.def.format.video.eColorFormat =
                isEncoder_ ? OMX_COLOR_FormatUnused : OMX_COLOR_FormatYUV420SemiPlanar;
        }
    }
}

bool VirtOmxComponent::LegalTransition(OMX_STATETYPE from, OMX_STATETYPE to) const
{
    switch (from) {
        case OMX_StateLoaded:
            return to == OMX_StateIdle;
        case OMX_StateIdle:
            return to == OMX_StateLoaded || to == OMX_StateExecuting;
        case OMX_StateExecuting:
            return to == OMX_StateIdle || to == OMX_StatePause;
        case OMX_StatePause:
            return to == OMX_StateExecuting || to == OMX_StateIdle;
        default:
            return false;
    }
}

bool VirtOmxComponent::TransitionReadyLocked(OMX_STATETYPE target) const
{
    if (target == OMX_StateIdle) {
        /* the Loaded to Idle transition completes when every enabled port is populated */
        for (uint32_t i = 0; i < PORT_COUNT; i++) {
            if (ports_[i].def.bEnabled && ports_[i].buffers.size() < ports_[i].def.nBufferCountActual) {
                return false;
            }
        }
        return true;
    }
    if (target == OMX_StateLoaded) {
        /* the Idle to Loaded transition completes when every port is empty */
        for (uint32_t i = 0; i < PORT_COUNT; i++) {
            if (!ports_[i].buffers.empty()) {
                return false;
            }
        }
        return true;
    }
    return true;
}

bool VirtOmxComponent::CanPopulateLocked() const
{
    /* buffers may be supplied in Loaded state and while switching to Idle */
    if (state_ == OMX_StateLoaded) {
        return !hasPendingState_ || pendingState_ == OMX_StateIdle;
    }
    return false;
}

bool VirtOmxComponent::CanReleaseLocked() const
{
    /* buffers may be released in Loaded state and while switching back from Idle */
    if (state_ == OMX_StateLoaded) {
        return true;
    }
    return state_ == OMX_StateIdle && hasPendingState_ && pendingState_ == OMX_StateLoaded;
}

bool VirtOmxComponent::IsSetParamStateLocked() const
{
    return state_ == OMX_StateLoaded || state_ == OMX_StateIdle;
}

VirtOmxComponent::BufferEntry *VirtOmxComponent::FindEntryLocked(uint32_t portIndex,
    OMX_BUFFERHEADERTYPE *buffer) const
{
    for (BufferEntry *entry : ports_[portIndex].buffers) {
        if (&entry->hdr == buffer) {
            return entry;
        }
    }
    return nullptr;
}

void VirtOmxComponent::DeleteEntry(BufferEntry *entry)
{
    if (entry == nullptr) {
        return;
    }
    if (entry->owned && entry->hdr.pBuffer != nullptr) {
        free(entry->hdr.pBuffer);
    }
    delete entry;
}

void VirtOmxComponent::PostTask(const Task &task)
{
    {
        std::lock_guard<std::mutex> lk(taskMtx_);
        tasks_.push_back(task);
    }
    taskCv_.notify_one();
}

void VirtOmxComponent::FireEventHandler(OMX_EVENTTYPE event, OMX_U32 data1, OMX_U32 data2, OMX_PTR eventData)
{
    OMX_CALLBACKTYPE cb;
    OMX_PTR appData;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        cb = callbacks_;
        appData = appData_;
    }
    if (cb.EventHandler != nullptr) {
        cb.EventHandler(&comp_, appData, event, data1, data2, eventData);
    }
}

void VirtOmxComponent::FireEmptyBufferDone(BufferEntry *entry)
{
    OMX_CALLBACKTYPE cb;
    OMX_PTR appData;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        cb = callbacks_;
        appData = appData_;
    }
    if (cb.EmptyBufferDone != nullptr) {
        cb.EmptyBufferDone(&comp_, appData, &entry->hdr);
    }
}

void VirtOmxComponent::FireFillBufferDone(BufferEntry *entry)
{
    OMX_CALLBACKTYPE cb;
    OMX_PTR appData;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        cb = callbacks_;
        appData = appData_;
    }
    if (cb.FillBufferDone != nullptr) {
        /* no content processing happens, report an empty output buffer */
        entry->hdr.nFilledLen = 0;
        entry->hdr.nOffset = 0;
        cb.FillBufferDone(&comp_, appData, &entry->hdr);
    }
}

void VirtOmxComponent::WorkerLoop()
{
    bool running = true;
    while (running) {
        Task task;
        {
            std::unique_lock<std::mutex> lk(taskMtx_);
            taskCv_.wait(lk, [this] { return !tasks_.empty() || !workerRunning_; });
            if (!workerRunning_ && tasks_.empty()) {
                running = false;
                continue;
            }
            if (tasks_.empty()) {
                continue;
            }
            task = tasks_.front();
            tasks_.pop_front();
        }
        HandleTask(task);
    }
}

void VirtOmxComponent::HandleTask(const Task &task)
{
    switch (task.kind) {
        case TaskKind::STATE_DONE: {
            {
                std::lock_guard<std::mutex> lk(mtx_);
                /* skip the update when a gated transition was already completed inline */
                if (hasPendingState_ && pendingState_ == static_cast<OMX_STATETYPE>(task.data1)) {
                    state_ = static_cast<OMX_STATETYPE>(task.data1);
                    hasPendingState_ = false;
                }
            }
            FireEventHandler(OMX_EventCmdComplete, OMX_CommandStateSet, task.data1, nullptr);
            break;
        }
        case TaskKind::CMD_DONE: {
            FireEventHandler(OMX_EventCmdComplete, task.data1, task.data2, nullptr);
            break;
        }
        case TaskKind::EMPTY_DONE: {
            FireEmptyBufferDone(task.entry);
            if (--task.entry->pending == 0 && task.entry->retired) {
                DeleteEntry(task.entry);
            }
            break;
        }
        case TaskKind::FILL_DONE: {
            FireFillBufferDone(task.entry);
            if (--task.entry->pending == 0 && task.entry->retired) {
                DeleteEntry(task.entry);
            }
            break;
        }
        default:
            break;
    }
}

void VirtOmxComponent::StopWorker()
{
    {
        std::lock_guard<std::mutex> lk(taskMtx_);
        workerRunning_ = false;
    }
    taskCv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

OMX_ERRORTYPE VirtOmxComponent::GetComponentVersion(OMX_STRING componentName,
    OMX_VERSIONTYPE *componentVersion, OMX_VERSIONTYPE *specVersion, OMX_UUIDTYPE *componentUuid)
{
    if (componentName == nullptr || componentVersion == nullptr || specVersion == nullptr ||
        componentUuid == nullptr) {
        return OMX_ErrorBadParameter;
    }
    if (strncpy_s(componentName, OMX_MAX_STRINGNAME_SIZE, name_.c_str(), name_.length()) != EOK) {
        return OMX_ErrorBadParameter;
    }
    componentVersion->nVersion = VIRT_COMPONENT_VERSION;
    specVersion->nVersion = OMX_SPEC_VERSION;
    if (memset_s(componentUuid, sizeof(OMX_UUIDTYPE), 0, sizeof(OMX_UUIDTYPE)) != EOK) {
        return OMX_ErrorBadParameter;
    }
    const char uuidTag[] = "virt-omx-software-component";
    if (memcpy_s(componentUuid, sizeof(OMX_UUIDTYPE), uuidTag, sizeof(uuidTag)) != EOK) {
        return OMX_ErrorBadParameter;
    }
    return OMX_ErrorNone;
}

OMX_ERRORTYPE VirtOmxComponent::HandleStateSetCommand(OMX_U32 param1)
{
    OMX_STATETYPE target = static_cast<OMX_STATETYPE>(param1);
    std::lock_guard<std::mutex> lk(mtx_);
    if (target != OMX_StateLoaded && target != OMX_StateIdle && target != OMX_StateExecuting &&
        target != OMX_StatePause) {
        return OMX_ErrorBadParameter;
    }
    if (hasPendingState_) {
        return OMX_ErrorIncorrectStateOperation;
    }
    if (!LegalTransition(state_, target)) {
        return OMX_ErrorIncorrectStateTransition;
    }
    hasPendingState_ = true;
    pendingState_ = target;
    if ((state_ == OMX_StateLoaded && target == OMX_StateIdle) ||
        (state_ == OMX_StateIdle && target == OMX_StateLoaded)) {
        /* gated on port population / buffer release */
        if (TransitionReadyLocked(target)) {
            PostTask({TaskKind::STATE_DONE, static_cast<OMX_U32>(target), 0, nullptr});
        }
    } else {
        PostTask({TaskKind::STATE_DONE, static_cast<OMX_U32>(target), 0, nullptr});
    }
    return OMX_ErrorNone;
}

OMX_ERRORTYPE VirtOmxComponent::SendCommand(OMX_COMMANDTYPE cmd, OMX_U32 param1, OMX_PTR cmdData)
{
    switch (cmd) {
        case OMX_CommandStateSet:
            return HandleStateSetCommand(param1);
        case OMX_CommandFlush:
        case OMX_CommandPortDisable:
        case OMX_CommandPortEnable: {
            if (param1 != 0xFFFFFFFF && param1 >= PORT_COUNT) { /* 0xFFFFFFFF: OMX_ALL */
                return OMX_ErrorBadPortIndex;
            }
            PostTask({TaskKind::CMD_DONE, static_cast<OMX_U32>(cmd), param1, nullptr});
            return OMX_ErrorNone;
        }
        case OMX_CommandMarkBuffer: {
            if (param1 >= PORT_COUNT) {
                return OMX_ErrorBadPortIndex;
            }
            if (cmdData == nullptr) {
                return OMX_ErrorBadParameter;
            }
            PostTask({TaskKind::CMD_DONE, static_cast<OMX_U32>(cmd), param1, nullptr});
            return OMX_ErrorNone;
        }
        default:
            return OMX_ErrorBadParameter;
    }
}

OMX_ERRORTYPE VirtOmxComponent::GetParameter(OMX_INDEXTYPE index, OMX_PTR structPtr)
{
    if (structPtr == nullptr) {
        return OMX_ErrorBadParameter;
    }
    switch (static_cast<uint32_t>(index)) {
        case OMX_IndexParamPortDefinition:
            return HandleGetPortDefinition(structPtr);
        case OMX_IndexParamVideoPortFormat:
            return HandleGetVideoPortFormat(structPtr);
        case OMX_IndexCodecVideoPortFormat:
            return HandleGetCodecVideoPortFormat(structPtr);
        case OMX_IndexParamUseBufferType:
            return HandleGetUseBufferType(structPtr);
        case OMX_IndexParamSupportBufferType:
            return HandleGetSupportBufferType(structPtr);
        case OMX_IndexParamStandardComponentRole:
            return HandleGetComponentRole(structPtr);
        case OMX_IndexParamVideoBitrate:
            return HandleGetVideoBitrateParam(structPtr);
        default:
            return OMX_ErrorUnsupportedIndex;
    }
}

OMX_ERRORTYPE VirtOmxComponent::SetParameter(OMX_INDEXTYPE index, OMX_PTR structPtr)
{
    if (structPtr == nullptr) {
        return OMX_ErrorBadParameter;
    }
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (!IsSetParamStateLocked()) {
            return OMX_ErrorIncorrectStateOperation;
        }
    }
    switch (static_cast<uint32_t>(index)) {
        case OMX_IndexParamPortDefinition:
            return HandleSetPortDefinition(structPtr);
        case OMX_IndexParamVideoPortFormat:
            return HandleSetVideoPortFormat(structPtr);
        case OMX_IndexCodecVideoPortFormat:
            return HandleSetCodecVideoPortFormat(structPtr);
        case OMX_IndexParamUseBufferType:
            return HandleSetUseBufferType(structPtr);
        case OMX_IndexParamVideoBitrate:
            return HandleSetVideoBitrateParam(structPtr);
        case OMX_IndexParamStandardComponentRole:
            return HandleSetComponentRole(structPtr);
        case OMX_IndexParamOverlayBuffer:
            return HandleSetOverlayBuffer(structPtr);
        case OMX_IndexParamLowLatency:
            return HandleSetLowLatency(structPtr);
        default:
            return OMX_ErrorUnsupportedIndex;
    }
}

OMX_ERRORTYPE VirtOmxComponent::GetConfig(OMX_INDEXTYPE index, OMX_PTR structPtr)
{
    if (structPtr == nullptr) {
        return OMX_ErrorBadParameter;
    }
    if (index != OMX_IndexConfigVideoBitrate) {
        return OMX_ErrorUnsupportedIndex;
    }
    if (!SizeMatches(structPtr, sizeof(OMX_VIDEO_CONFIG_BITRATETYPE))) {
        return OMX_ErrorBadParameter;
    }
    auto *param = static_cast<OMX_VIDEO_CONFIG_BITRATETYPE *>(structPtr);
    std::lock_guard<std::mutex> lk(mtx_);
    if (param->nPortIndex != PORT_OUTPUT) {
        return OMX_ErrorBadParameter;
    }
    param->nEncodeBitrate = bitrateConfig_.nEncodeBitrate;
    return OMX_ErrorNone;
}

OMX_ERRORTYPE VirtOmxComponent::SetConfig(OMX_INDEXTYPE index, OMX_PTR structPtr)
{
    if (structPtr == nullptr) {
        return OMX_ErrorBadParameter;
    }
    if (index != OMX_IndexConfigVideoBitrate) {
        return OMX_ErrorUnsupportedIndex;
    }
    if (!SizeMatches(structPtr, sizeof(OMX_VIDEO_CONFIG_BITRATETYPE))) {
        return OMX_ErrorBadParameter;
    }
    auto *param = static_cast<OMX_VIDEO_CONFIG_BITRATETYPE *>(structPtr);
    std::lock_guard<std::mutex> lk(mtx_);
    if (param->nPortIndex != PORT_OUTPUT) {
        return OMX_ErrorBadParameter;
    }
    bitrateConfig_.nEncodeBitrate = param->nEncodeBitrate;
    return OMX_ErrorNone;
}

OMX_ERRORTYPE VirtOmxComponent::GetExtensionIndex(OMX_STRING parameterName, OMX_INDEXTYPE *index)
{
    (void)parameterName;
    (void)index;
    /* the software component provides no vendor extension strings */
    return OMX_ErrorUnsupportedIndex;
}

OMX_ERRORTYPE VirtOmxComponent::GetState(OMX_STATETYPE *state)
{
    if (state == nullptr) {
        return OMX_ErrorBadParameter;
    }
    std::lock_guard<std::mutex> lk(mtx_);
    *state = state_;
    return OMX_ErrorNone;
}

OMX_ERRORTYPE VirtOmxComponent::ComponentTunnelRequest(OMX_U32 port, OMX_HANDLETYPE tunneledComp,
    OMX_U32 tunneledPort, OMX_TUNNELSETUPTYPE *tunnelSetup)
{
    (void)port;
    (void)tunneledComp;
    (void)tunneledPort;
    (void)tunnelSetup;
    return OMX_ErrorTunnelingUnsupported;
}

OMX_ERRORTYPE VirtOmxComponent::UseBuffer(OMX_BUFFERHEADERTYPE **bufferHdr, OMX_U32 portIndex,
    OMX_PTR appPrivate, OMX_U32 sizeBytes, OMX_U8 *buffer)
{
    if (bufferHdr == nullptr || buffer == nullptr) {
        return OMX_ErrorBadParameter;
    }
    std::lock_guard<std::mutex> lk(mtx_);
    if (portIndex >= PORT_COUNT) {
        return OMX_ErrorBadPortIndex;
    }
    if (!CanPopulateLocked()) {
        return OMX_ErrorIncorrectStateOperation;
    }
    VirtPort &port = ports_[portIndex];
    if (port.buffers.size() >= port.def.nBufferCountActual) {
        return OMX_ErrorInsufficientResources;
    }
    auto *entry = new (std::nothrow) BufferEntry(false);
    if (entry == nullptr) {
        return OMX_ErrorInsufficientResources;
    }
    entry->hdr.nSize = sizeof(OMX_BUFFERHEADERTYPE);
    entry->hdr.nVersion.nVersion = OMX_SPEC_VERSION;
    entry->hdr.pBuffer = buffer;
    entry->hdr.nAllocLen = sizeBytes;
    entry->hdr.nFilledLen = 0;
    entry->hdr.nOffset = 0;
    entry->hdr.pAppPrivate = appPrivate;
    entry->hdr.pPlatformPrivate = nullptr;
    entry->hdr.pInputPortPrivate = nullptr;
    entry->hdr.pOutputPortPrivate = nullptr;
    entry->hdr.nInputPortIndex = (portIndex == PORT_INPUT) ? portIndex : 0x7FFFFFFF;
    entry->hdr.nOutputPortIndex = (portIndex == PORT_OUTPUT) ? portIndex : 0x7FFFFFFF;
    port.buffers.push_back(entry);
    port.def.bPopulated = (port.buffers.size() >= port.def.nBufferCountActual) ? OMX_TRUE : OMX_FALSE;
    *bufferHdr = &entry->hdr;
    if (hasPendingState_ && pendingState_ == OMX_StateIdle && TransitionReadyLocked(OMX_StateIdle)) {
        /* complete the gated transition inline; the queued task only reports the event */
        state_ = OMX_StateIdle;
        hasPendingState_ = false;
        PostTask({TaskKind::STATE_DONE, static_cast<OMX_U32>(OMX_StateIdle), 0, nullptr});
    }
    return OMX_ErrorNone;
}

OMX_ERRORTYPE VirtOmxComponent::AllocateBuffer(OMX_BUFFERHEADERTYPE **bufferHdr, OMX_U32 portIndex,
    OMX_PTR appPrivate, OMX_U32 sizeBytes)
{
    if (bufferHdr == nullptr) {
        return OMX_ErrorBadParameter;
    }
    std::lock_guard<std::mutex> lk(mtx_);
    if (portIndex >= PORT_COUNT) {
        return OMX_ErrorBadPortIndex;
    }
    if (!CanPopulateLocked()) {
        return OMX_ErrorIncorrectStateOperation;
    }
    VirtPort &port = ports_[portIndex];
    if (port.buffers.size() >= port.def.nBufferCountActual) {
        return OMX_ErrorInsufficientResources;
    }
    auto *entry = new (std::nothrow) BufferEntry(true);
    if (entry == nullptr) {
        return OMX_ErrorInsufficientResources;
    }
    constexpr uint32_t maxBufferBytes = 64 * 1024 * 1024; /* 64 MiB cap for a simulated port buffer */
    if (sizeBytes == 0 || sizeBytes > maxBufferBytes) {
        delete entry;
        return OMX_ErrorBadParameter;
    }
    entry->hdr.pBuffer = static_cast<OMX_U8 *>(malloc(sizeBytes));
    if (entry->hdr.pBuffer == nullptr) {
        delete entry;
        return OMX_ErrorInsufficientResources;
    }
    entry->hdr.nSize = sizeof(OMX_BUFFERHEADERTYPE);
    entry->hdr.nVersion.nVersion = OMX_SPEC_VERSION;
    entry->hdr.nAllocLen = sizeBytes;
    entry->hdr.nFilledLen = 0;
    entry->hdr.nOffset = 0;
    entry->hdr.pAppPrivate = appPrivate;
    entry->hdr.pPlatformPrivate = nullptr;
    entry->hdr.pInputPortPrivate = nullptr;
    entry->hdr.pOutputPortPrivate = nullptr;
    entry->hdr.nInputPortIndex = (portIndex == PORT_INPUT) ? portIndex : 0x7FFFFFFF;
    entry->hdr.nOutputPortIndex = (portIndex == PORT_OUTPUT) ? portIndex : 0x7FFFFFFF;
    port.buffers.push_back(entry);
    port.def.bPopulated = (port.buffers.size() >= port.def.nBufferCountActual) ? OMX_TRUE : OMX_FALSE;
    *bufferHdr = &entry->hdr;
    if (hasPendingState_ && pendingState_ == OMX_StateIdle && TransitionReadyLocked(OMX_StateIdle)) {
        /* complete the gated transition inline; the queued task only reports the event */
        state_ = OMX_StateIdle;
        hasPendingState_ = false;
        PostTask({TaskKind::STATE_DONE, static_cast<OMX_U32>(OMX_StateIdle), 0, nullptr});
    }
    return OMX_ErrorNone;
}

OMX_ERRORTYPE VirtOmxComponent::FreeBuffer(OMX_U32 portIndex, OMX_BUFFERHEADERTYPE *buffer)
{
    if (buffer == nullptr) {
        return OMX_ErrorBadParameter;
    }
    bool deleteNow = false;
    BufferEntry *entry = nullptr;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (portIndex >= PORT_COUNT) {
            return OMX_ErrorBadPortIndex;
        }
        if (!CanReleaseLocked()) {
            return OMX_ErrorIncorrectStateOperation;
        }
        entry = FindEntryLocked(portIndex, buffer);
        if (entry == nullptr) {
            return OMX_ErrorBadParameter;
        }
        ports_[portIndex].buffers.erase(
            std::remove(ports_[portIndex].buffers.begin(), ports_[portIndex].buffers.end(), entry),
            ports_[portIndex].buffers.end());
        ports_[portIndex].def.bPopulated =
            (ports_[portIndex].buffers.size() >= ports_[portIndex].def.nBufferCountActual) ? OMX_TRUE : OMX_FALSE;
        entry->retired = true;
        deleteNow = (entry->pending == 0);
        if (hasPendingState_ && pendingState_ == OMX_StateLoaded && TransitionReadyLocked(OMX_StateLoaded)) {
            /* complete the gated transition inline; the queued task only reports the event */
            state_ = OMX_StateLoaded;
            hasPendingState_ = false;
            PostTask({TaskKind::STATE_DONE, static_cast<OMX_U32>(OMX_StateLoaded), 0, nullptr});
        }
    }
    if (deleteNow) {
        DeleteEntry(entry);
    }
    return OMX_ErrorNone;
}

OMX_ERRORTYPE VirtOmxComponent::EmptyThisBuffer(OMX_BUFFERHEADERTYPE *buffer)
{
    if (buffer == nullptr) {
        return OMX_ErrorBadParameter;
    }
    std::lock_guard<std::mutex> lk(mtx_);
    if (state_ != OMX_StateExecuting) {
        return OMX_ErrorIncorrectStateOperation;
    }
    BufferEntry *entry = FindEntryLocked(PORT_INPUT, buffer);
    if (entry == nullptr) {
        return OMX_ErrorBadParameter;
    }
    entry->pending++;
    PostTask({TaskKind::EMPTY_DONE, 0, 0, entry});
    return OMX_ErrorNone;
}

OMX_ERRORTYPE VirtOmxComponent::FillThisBuffer(OMX_BUFFERHEADERTYPE *buffer)
{
    if (buffer == nullptr) {
        return OMX_ErrorBadParameter;
    }
    std::lock_guard<std::mutex> lk(mtx_);
    if (state_ != OMX_StateExecuting) {
        return OMX_ErrorIncorrectStateOperation;
    }
    BufferEntry *entry = FindEntryLocked(PORT_OUTPUT, buffer);
    if (entry == nullptr) {
        return OMX_ErrorBadParameter;
    }
    entry->pending++;
    PostTask({TaskKind::FILL_DONE, 0, 0, entry});
    return OMX_ErrorNone;
}

OMX_ERRORTYPE VirtOmxComponent::SetCallbacks(OMX_CALLBACKTYPE *callbacks, OMX_PTR appData)
{
    if (callbacks == nullptr) {
        return OMX_ErrorBadParameter;
    }
    std::lock_guard<std::mutex> lk(mtx_);
    callbacks_ = *callbacks;
    appData_ = appData;
    comp_.pApplicationPrivate = appData;
    return OMX_ErrorNone;
}

OMX_ERRORTYPE VirtOmxComponent::ComponentDeInit()
{
    /* release every buffer regardless of the current state, the IL client may
     * tear the component down without completing the state machine */
    StopWorker();
    {
        std::lock_guard<std::mutex> lk(mtx_);
        for (uint32_t port = 0; port < PORT_COUNT; port++) {
            for (BufferEntry *entry : ports_[port].buffers) {
                DeleteEntry(entry);
            }
            ports_[port].buffers.clear();
            ports_[port].def.bPopulated = OMX_FALSE;
        }
        state_ = OMX_StateLoaded;
        hasPendingState_ = false;
    }
    return OMX_ErrorNone;
}

OMX_ERRORTYPE VirtOmxComponent::UseEglImage(OMX_BUFFERHEADERTYPE **bufferHdr, OMX_U32 portIndex,
    OMX_PTR appPrivate, OMX_PTR eglImage)
{
    (void)bufferHdr;
    (void)portIndex;
    (void)appPrivate;
    (void)eglImage;
    return OMX_ErrorNotImplemented;
}

OMX_ERRORTYPE VirtOmxComponent::ComponentRoleEnum(OMX_U8 *role, OMX_U32 index)
{
    if (role == nullptr) {
        return OMX_ErrorBadParameter;
    }
    if (index != 0) {
        return OMX_ErrorNoMore;
    }
    if (strncpy_s(reinterpret_cast<char *>(role), OMX_MAX_STRINGNAME_SIZE, role_.c_str(),
        role_.length()) != EOK) {
        return OMX_ErrorBadParameter;
    }
    return OMX_ErrorNone;
}

OMX_ERRORTYPE VirtOmxComponent::HandleGetPortDefinition(OMX_PTR structPtr)
{
    if (!SizeMatches(structPtr, sizeof(OMX_PARAM_PORTDEFINITIONTYPE))) {
        return OMX_ErrorBadParameter;
    }
    auto *param = static_cast<OMX_PARAM_PORTDEFINITIONTYPE *>(structPtr);
    std::lock_guard<std::mutex> lk(mtx_);
    if (param->nPortIndex >= PORT_COUNT) {
        return OMX_ErrorBadPortIndex;
    }
    *param = ports_[param->nPortIndex].def;
    return OMX_ErrorNone;
}

OMX_ERRORTYPE VirtOmxComponent::HandleSetPortDefinition(OMX_PTR structPtr)
{
    if (!SizeMatches(structPtr, sizeof(OMX_PARAM_PORTDEFINITIONTYPE))) {
        return OMX_ErrorBadParameter;
    }
    auto *param = static_cast<OMX_PARAM_PORTDEFINITIONTYPE *>(structPtr);
    std::lock_guard<std::mutex> lk(mtx_);
    if (param->nPortIndex >= PORT_COUNT) {
        return OMX_ErrorBadPortIndex;
    }
    if (param->nBufferCountActual < ports_[param->nPortIndex].def.nBufferCountMin) {
        return OMX_ErrorBadParameter;
    }
    if (!ports_[param->nPortIndex].buffers.empty()) {
        /* port geometry can not be changed while buffers are outstanding */
        return OMX_ErrorIncorrectStateOperation;
    }
    OMX_PARAM_PORTDEFINITIONTYPE &def = ports_[param->nPortIndex].def;
    def.nBufferCountActual = param->nBufferCountActual;
    if (param->nBufferSize > 0) {
        def.nBufferSize = param->nBufferSize;
    }
    if (param->format.video.nFrameWidth > 0 && param->format.video.nFrameHeight > 0) {
        def.format.video.nFrameWidth = param->format.video.nFrameWidth;
        def.format.video.nFrameHeight = param->format.video.nFrameHeight;
        def.format.video.nStride = param->format.video.nStride;
        def.format.video.nSliceHeight = param->format.video.nSliceHeight;
        if (param->format.video.eColorFormat != OMX_COLOR_FormatUnused) {
            def.format.video.eColorFormat = param->format.video.eColorFormat;
        }
        if (param->format.video.eCompressionFormat != OMX_VIDEO_CodingUnused) {
            def.format.video.eCompressionFormat = param->format.video.eCompressionFormat;
        }
    }
    return OMX_ErrorNone;
}

OMX_ERRORTYPE VirtOmxComponent::HandleGetVideoPortFormat(OMX_PTR structPtr)
{
    if (!SizeMatches(structPtr, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE))) {
        return OMX_ErrorBadParameter;
    }
    auto *param = static_cast<OMX_VIDEO_PARAM_PORTFORMATTYPE *>(structPtr);
    std::lock_guard<std::mutex> lk(mtx_);
    if (param->nPortIndex >= PORT_COUNT) {
        return OMX_ErrorBadPortIndex;
    }
    const OMX_PARAM_PORTDEFINITIONTYPE &def = ports_[param->nPortIndex].def;
    param->eCompressionFormat = def.format.video.eCompressionFormat;
    param->eColorFormat = def.format.video.eColorFormat;
    param->xFramerate = def.format.video.xFramerate;
    return OMX_ErrorNone;
}

OMX_ERRORTYPE VirtOmxComponent::HandleSetVideoPortFormat(OMX_PTR structPtr)
{
    if (!SizeMatches(structPtr, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE))) {
        return OMX_ErrorBadParameter;
    }
    auto *param = static_cast<OMX_VIDEO_PARAM_PORTFORMATTYPE *>(structPtr);
    std::lock_guard<std::mutex> lk(mtx_);
    if (param->nPortIndex >= PORT_COUNT) {
        return OMX_ErrorBadPortIndex;
    }
    OMX_PARAM_PORTDEFINITIONTYPE &def = ports_[param->nPortIndex].def;
    def.format.video.eCompressionFormat = param->eCompressionFormat;
    def.format.video.eColorFormat = param->eColorFormat;
    if (param->xFramerate > 0) {
        def.format.video.xFramerate = param->xFramerate;
    }
    return OMX_ErrorNone;
}

OMX_ERRORTYPE VirtOmxComponent::HandleGetCodecVideoPortFormat(OMX_PTR structPtr)
{
    if (!SizeMatches(structPtr, sizeof(CodecVideoPortFormatParam))) {
        return OMX_ErrorBadParameter;
    }
    auto *param = static_cast<CodecVideoPortFormatParam *>(structPtr);
    std::lock_guard<std::mutex> lk(mtx_);
    if (param->portIndex >= PORT_COUNT) {
        return OMX_ErrorBadPortIndex;
    }
    const OMX_PARAM_PORTDEFINITIONTYPE &def = ports_[param->portIndex].def;
    param->codecColorFormat = PIXEL_FMT_YCBCR_420_SP_VALUE;
    param->codecCompressFormat = static_cast<uint32_t>(def.format.video.eCompressionFormat);
    param->framerate = def.format.video.xFramerate;
    return OMX_ErrorNone;
}

OMX_ERRORTYPE VirtOmxComponent::HandleSetCodecVideoPortFormat(OMX_PTR structPtr)
{
    if (!SizeMatches(structPtr, sizeof(CodecVideoPortFormatParam))) {
        return OMX_ErrorBadParameter;
    }
    auto *param = static_cast<CodecVideoPortFormatParam *>(structPtr);
    std::lock_guard<std::mutex> lk(mtx_);
    if (param->portIndex >= PORT_COUNT) {
        return OMX_ErrorBadPortIndex;
    }
    return OMX_ErrorNone;
}

OMX_ERRORTYPE VirtOmxComponent::HandleGetUseBufferType(OMX_PTR structPtr)
{
    if (!SizeMatches(structPtr, sizeof(UseBufferType))) {
        return OMX_ErrorBadParameter;
    }
    auto *param = static_cast<UseBufferType *>(structPtr);
    std::lock_guard<std::mutex> lk(mtx_);
    if (param->portIndex >= PORT_COUNT) {
        return OMX_ErrorBadPortIndex;
    }
    param->bufferType = ports_[param->portIndex].useBufferType;
    return OMX_ErrorNone;
}

OMX_ERRORTYPE VirtOmxComponent::HandleSetUseBufferType(OMX_PTR structPtr)
{
    if (!SizeMatches(structPtr, sizeof(UseBufferType))) {
        return OMX_ErrorBadParameter;
    }
    auto *param = static_cast<UseBufferType *>(structPtr);
    std::lock_guard<std::mutex> lk(mtx_);
    if (param->portIndex >= PORT_COUNT) {
        return OMX_ErrorBadPortIndex;
    }
    ports_[param->portIndex].useBufferType = param->bufferType;
    return OMX_ErrorNone;
}

OMX_ERRORTYPE VirtOmxComponent::HandleGetSupportBufferType(OMX_PTR structPtr)
{
    if (!SizeMatches(structPtr, sizeof(SupportBufferType))) {
        return OMX_ErrorBadParameter;
    }
    auto *param = static_cast<SupportBufferType *>(structPtr);
    std::lock_guard<std::mutex> lk(mtx_);
    if (param->portIndex >= PORT_COUNT) {
        return OMX_ErrorBadPortIndex;
    }
    param->bufferTypes = SUPPORTED_BUFFER_TYPES;
    return OMX_ErrorNone;
}

OMX_ERRORTYPE VirtOmxComponent::HandleGetVideoBitrateParam(OMX_PTR structPtr)
{
    if (!SizeMatches(structPtr, sizeof(OMX_VIDEO_PARAM_BITRATETYPE))) {
        return OMX_ErrorBadParameter;
    }
    auto *param = static_cast<OMX_VIDEO_PARAM_BITRATETYPE *>(structPtr);
    std::lock_guard<std::mutex> lk(mtx_);
    if (param->nPortIndex != PORT_OUTPUT) {
        return OMX_ErrorBadParameter;
    }
    param->eControlRate = bitrateParam_.eControlRate;
    param->nTargetBitrate = bitrateParam_.nTargetBitrate;
    return OMX_ErrorNone;
}

OMX_ERRORTYPE VirtOmxComponent::HandleSetVideoBitrateParam(OMX_PTR structPtr)
{
    if (!SizeMatches(structPtr, sizeof(OMX_VIDEO_PARAM_BITRATETYPE))) {
        return OMX_ErrorBadParameter;
    }
    auto *param = static_cast<OMX_VIDEO_PARAM_BITRATETYPE *>(structPtr);
    std::lock_guard<std::mutex> lk(mtx_);
    if (param->nPortIndex != PORT_OUTPUT) {
        return OMX_ErrorBadParameter;
    }
    bitrateParam_.eControlRate = param->eControlRate;
    bitrateParam_.nTargetBitrate = param->nTargetBitrate;
    return OMX_ErrorNone;
}

OMX_ERRORTYPE VirtOmxComponent::HandleGetComponentRole(OMX_PTR structPtr)
{
    if (!SizeMatches(structPtr, sizeof(OMX_PARAM_COMPONENTROLETYPE))) {
        return OMX_ErrorBadParameter;
    }
    auto *param = static_cast<OMX_PARAM_COMPONENTROLETYPE *>(structPtr);
    const std::string &role = componentRoleParam_.empty() ? role_ : componentRoleParam_;
    if (strncpy_s(reinterpret_cast<char *>(param->cRole), OMX_MAX_STRINGNAME_SIZE, role.c_str(),
        role.length()) != EOK) {
        return OMX_ErrorBadParameter;
    }
    return OMX_ErrorNone;
}

OMX_ERRORTYPE VirtOmxComponent::HandleSetComponentRole(OMX_PTR structPtr)
{
    if (!SizeMatches(structPtr, sizeof(OMX_PARAM_COMPONENTROLETYPE))) {
        return OMX_ErrorBadParameter;
    }
    auto *param = static_cast<OMX_PARAM_COMPONENTROLETYPE *>(structPtr);
    std::lock_guard<std::mutex> lk(mtx_);
    componentRoleParam_ = reinterpret_cast<const char *>(param->cRole);
    return OMX_ErrorNone;
}

OMX_ERRORTYPE VirtOmxComponent::HandleSetOverlayBuffer(OMX_PTR structPtr)
{
    if (!SizeMatches(structPtr, sizeof(CodecParamOverlayBuffer))) {
        return OMX_ErrorBadParameter;
    }
    auto *param = static_cast<CodecParamOverlayBuffer *>(structPtr);
    std::lock_guard<std::mutex> lk(mtx_);
    overlayParam_ = *param;
    return OMX_ErrorNone;
}

OMX_ERRORTYPE VirtOmxComponent::HandleSetLowLatency(OMX_PTR structPtr)
{
    if (!SizeMatches(structPtr, sizeof(OMX_CONFIG_BOOLEANTYPE))) {
        return OMX_ErrorBadParameter;
    }
    auto *param = static_cast<OMX_CONFIG_BOOLEANTYPE *>(structPtr);
    std::lock_guard<std::mutex> lk(mtx_);
    lowLatency_ = (param->bEnabled == OMX_TRUE);
    return OMX_ErrorNone;
}

} // namespace OMX
} // namespace VDI
} // namespace OHOS
