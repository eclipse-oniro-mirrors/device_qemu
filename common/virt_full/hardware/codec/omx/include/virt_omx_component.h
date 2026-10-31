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

#ifndef VIRT_OMX_COMPONENT_H
#define VIRT_OMX_COMPONENT_H

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <OMX_Component.h>
#include <OMX_Core.h>
#include <OMX_Types.h>
#include <OMX_Video.h>
#include "codec_omx_ext.h"

namespace OHOS {
namespace VDI {
namespace OMX {

/*
 * Software contract-grade OMX IL component for the virtual product.
 *
 * The component implements the OpenMAX IL 1.1.2 state machine, parameter and
 * buffer management contracts so that the codec HDI service and its clients
 * can drive it exactly like a real hardware component. It performs NO bitstream
 * content processing: buffers submitted through EmptyThisBuffer/FillThisBuffer
 * are returned to the IL client through the standard done-callbacks without
 * being decoded or encoded.
 */
class VirtOmxComponent {
public:
    VirtOmxComponent(const std::string &name, const std::string &role, bool isEncoder);
    ~VirtOmxComponent();

    bool Init(OMX_PTR appData, const OMX_CALLBACKTYPE *callbacks);
    OMX_COMPONENTTYPE *ComponentHandle();

    /* OMX_COMPONENTTYPE vtable entry points */
    OMX_ERRORTYPE GetComponentVersion(OMX_STRING componentName, OMX_VERSIONTYPE *componentVersion,
        OMX_VERSIONTYPE *specVersion, OMX_UUIDTYPE *componentUuid);
    OMX_ERRORTYPE SendCommand(OMX_COMMANDTYPE cmd, OMX_U32 param1, OMX_PTR cmdData);
    OMX_ERRORTYPE GetParameter(OMX_INDEXTYPE index, OMX_PTR structPtr);
    OMX_ERRORTYPE SetParameter(OMX_INDEXTYPE index, OMX_PTR structPtr);
    OMX_ERRORTYPE GetConfig(OMX_INDEXTYPE index, OMX_PTR structPtr);
    OMX_ERRORTYPE SetConfig(OMX_INDEXTYPE index, OMX_PTR structPtr);
    OMX_ERRORTYPE GetExtensionIndex(OMX_STRING parameterName, OMX_INDEXTYPE *index);
    OMX_ERRORTYPE GetState(OMX_STATETYPE *state);
    OMX_ERRORTYPE ComponentTunnelRequest(OMX_U32 port, OMX_HANDLETYPE tunneledComp,
        OMX_U32 tunneledPort, OMX_TUNNELSETUPTYPE *tunnelSetup);
    OMX_ERRORTYPE UseBuffer(OMX_BUFFERHEADERTYPE **bufferHdr, OMX_U32 portIndex, OMX_PTR appPrivate,
        OMX_U32 sizeBytes, OMX_U8 *buffer);
    OMX_ERRORTYPE AllocateBuffer(OMX_BUFFERHEADERTYPE **bufferHdr, OMX_U32 portIndex, OMX_PTR appPrivate,
        OMX_U32 sizeBytes);
    OMX_ERRORTYPE FreeBuffer(OMX_U32 portIndex, OMX_BUFFERHEADERTYPE *buffer);
    OMX_ERRORTYPE EmptyThisBuffer(OMX_BUFFERHEADERTYPE *buffer);
    OMX_ERRORTYPE FillThisBuffer(OMX_BUFFERHEADERTYPE *buffer);
    OMX_ERRORTYPE SetCallbacks(OMX_CALLBACKTYPE *callbacks, OMX_PTR appData);
    OMX_ERRORTYPE ComponentDeInit();
    OMX_ERRORTYPE UseEglImage(OMX_BUFFERHEADERTYPE **bufferHdr, OMX_U32 portIndex, OMX_PTR appPrivate,
        OMX_PTR eglImage);
    OMX_ERRORTYPE ComponentRoleEnum(OMX_U8 *role, OMX_U32 index);

    static VirtOmxComponent *Self(OMX_HANDLETYPE hComponent);
    struct BufferEntry {
        OMX_BUFFERHEADERTYPE hdr;
        bool owned;                       /* buffer memory allocated by the component */
        std::atomic<uint32_t> pending;    /* done-callbacks still in flight */
        bool retired;                     /* removed from the port, delete when pending hits 0 */

        explicit BufferEntry(bool bufferOwned) : owned(bufferOwned), pending(0), retired(false) {}
    };

    struct VirtPort {
        OMX_PARAM_PORTDEFINITIONTYPE def;
        uint32_t useBufferType;           /* set through OMX_IndexParamUseBufferType */
        std::vector<BufferEntry *> buffers;
    };

    enum class TaskKind {
        STATE_DONE,    /* a StateSet transition completed */
        CMD_DONE,      /* a non-state command completed */
        EMPTY_DONE,    /* return an input buffer to the IL client */
        FILL_DONE,     /* return an output buffer to the IL client */
    };

    struct Task {
        TaskKind kind;
        OMX_U32 data1;
        OMX_U32 data2;
        BufferEntry *entry;
    };

    static constexpr uint32_t PORT_INPUT = 0;
    static constexpr uint32_t PORT_OUTPUT = 1;
    static constexpr uint32_t PORT_COUNT = 2;
    static constexpr uint32_t DEFAULT_BUFFER_COUNT = 4;
    static constexpr uint32_t DEFAULT_RAW_WIDTH = 640;
    static constexpr uint32_t DEFAULT_RAW_HEIGHT = 480;
    static constexpr uint32_t DEFAULT_FRAMERATE_Q16 = 30u << 16; /* Q16 format */

    static BufferEntry *EntryOf(OMX_BUFFERHEADERTYPE *buffer);

    void InitPorts();
    void ResetPortDefs();
    bool LegalTransition(OMX_STATETYPE from, OMX_STATETYPE to) const;
    bool TransitionReadyLocked(OMX_STATETYPE target) const;
    bool CanPopulateLocked() const;
    bool CanReleaseLocked() const;
    bool IsSetParamStateLocked() const;
    BufferEntry *FindEntryLocked(uint32_t portIndex, OMX_BUFFERHEADERTYPE *buffer) const;
    void DeleteEntry(BufferEntry *entry);
    void PostTask(const Task &task);
    void FireEventHandler(OMX_EVENTTYPE event, OMX_U32 data1, OMX_U32 data2, OMX_PTR eventData);
    void FireEmptyBufferDone(BufferEntry *entry);
    void FireFillBufferDone(BufferEntry *entry);
    void WorkerLoop();
    void StopWorker();

    OMX_ERRORTYPE HandleStateSetCommand(OMX_U32 param1);
    void HandleTask(const Task &task);
    OMX_ERRORTYPE HandleGetPortDefinition(OMX_PTR structPtr);
    OMX_ERRORTYPE HandleSetPortDefinition(OMX_PTR structPtr);
    OMX_ERRORTYPE HandleGetVideoPortFormat(OMX_PTR structPtr);
    OMX_ERRORTYPE HandleSetVideoPortFormat(OMX_PTR structPtr);
    OMX_ERRORTYPE HandleGetCodecVideoPortFormat(OMX_PTR structPtr);
    OMX_ERRORTYPE HandleSetCodecVideoPortFormat(OMX_PTR structPtr);
    OMX_ERRORTYPE HandleGetUseBufferType(OMX_PTR structPtr);
    OMX_ERRORTYPE HandleSetUseBufferType(OMX_PTR structPtr);
    OMX_ERRORTYPE HandleGetSupportBufferType(OMX_PTR structPtr);
    OMX_ERRORTYPE HandleGetVideoBitrateParam(OMX_PTR structPtr);
    OMX_ERRORTYPE HandleSetVideoBitrateParam(OMX_PTR structPtr);
    OMX_ERRORTYPE HandleGetComponentRole(OMX_PTR structPtr);
    OMX_ERRORTYPE HandleSetComponentRole(OMX_PTR structPtr);
    OMX_ERRORTYPE HandleSetOverlayBuffer(OMX_PTR structPtr);
    OMX_ERRORTYPE HandleSetLowLatency(OMX_PTR structPtr);

    OMX_COMPONENTTYPE comp_;
    std::string name_;
    std::string role_;
    bool isEncoder_;

    std::mutex mtx_;                       /* protects state, ports and param storage */
    OMX_STATETYPE state_;
    bool hasPendingState_;
    OMX_STATETYPE pendingState_;
    OMX_CALLBACKTYPE callbacks_;
    OMX_PTR appData_;
    VirtPort ports_[PORT_COUNT];

    /* parameter/config storage */
    OMX_VIDEO_PARAM_BITRATETYPE bitrateParam_;
    OMX_VIDEO_CONFIG_BITRATETYPE bitrateConfig_;
    CodecParamOverlayBuffer overlayParam_;
    bool lowLatency_;
    std::string componentRoleParam_;

    /* async worker */
    std::mutex taskMtx_;
    std::condition_variable taskCv_;
    std::deque<Task> tasks_;
    std::thread worker_;
    bool workerRunning_;
};

} // namespace OMX
} // namespace VDI
} // namespace OHOS

#endif // VIRT_OMX_COMPONENT_H
