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

#include "sensor_sim_vdi.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>

#include "securec.h"
#include "sensor_type.h"

namespace OHOS {
namespace HDI {
namespace Sensor {
namespace V1_1 {

namespace {
    static SensorHandle MakeHandle(int32_t deviceId, int32_t sensorType, int32_t sensorId, int32_t location)
    {
        SensorHandle handle = {};
        handle.deviceId = deviceId;
        handle.sensorType = sensorType;
        handle.sensorId = sensorId;
        handle.location = location;
        return handle;
    }

    /* Sensors provided by the virtual device, in list order. */
    const std::vector<int32_t> SIM_SENSOR_TYPES = {
        SENSOR_TYPE_ACCELEROMETER,        /* 1, must stay first: multi-client tests use info[0] */
        SENSOR_TYPE_GYROSCOPE,            /* 2 */
        SENSOR_TYPE_AMBIENT_LIGHT,        /* 5 */
        SENSOR_TYPE_MAGNETIC_FIELD,       /* 6 */
        SENSOR_TYPE_BAROMETER,            /* 8 */
        SENSOR_TYPE_TEMPERATURE,          /* 9 */
        SENSOR_TYPE_HALL,                 /* 10 */
        SENSOR_TYPE_PROXIMITY,            /* 12 */
        SENSOR_TYPE_HUMIDITY,             /* 13 */
        SENSOR_TYPE_GRAVITY,              /* 257 */
        SENSOR_TYPE_LINEAR_ACCELERATION,  /* 258 */
        SENSOR_TYPE_ROTATION_VECTOR,      /* 259 */
    };

    struct SimSampleDef {
        float values[6];
        uint32_t dimension;
    };

    /* Plausible, always-in-range values for every simulated sensor. */
    const std::map<int32_t, SimSampleDef> SIM_SAMPLES = {
        { SENSOR_TYPE_ACCELEROMETER, { { 0.0f, 9.8f, 0.0f, 0.0f, 0.0f, 0.0f }, 3 } },
        { SENSOR_TYPE_GYROSCOPE, { { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 3 } },
        { SENSOR_TYPE_AMBIENT_LIGHT, { { 100.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 1 } },
        { SENSOR_TYPE_MAGNETIC_FIELD, { { 45.0f, 30.0f, 15.0f, 0.0f, 0.0f, 0.0f }, 3 } },
        { SENSOR_TYPE_BAROMETER, { { 1013.25f, 25.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 2 } },
        { SENSOR_TYPE_TEMPERATURE, { { 25.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 1 } },
        { SENSOR_TYPE_HALL, { { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 1 } },
        { SENSOR_TYPE_PROXIMITY, { { 5.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 1 } },
        { SENSOR_TYPE_HUMIDITY, { { 50.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 1 } },
        { SENSOR_TYPE_GRAVITY, { { 0.0f, 9.8f, 0.0f, 0.0f, 0.0f, 0.0f }, 3 } },
        { SENSOR_TYPE_LINEAR_ACCELERATION, { { 0.1f, 0.1f, 0.1f, 0.0f, 0.0f, 0.0f }, 3 } },
        { SENSOR_TYPE_ROTATION_VECTOR, { { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f }, 4 } },
    };

    constexpr int64_t MIN_DELAY_NS = 1000000;      /* 1 ms, covers the 2 ms fastest test interval */
    constexpr int64_t MAX_DELAY_NS = 1000000000;   /* 1 s */
    constexpr int32_t FIFO_MAX_EVENT_COUNT = 32;   /* reported hardware FIFO depth */
}

const std::vector<int32_t>& SensorSimVdi::GetSupportedTypes()
{
    return SIM_SENSOR_TYPES;
}

bool SensorSimVdi::IsSupportedType(int32_t sensorType)
{
    return std::find(SIM_SENSOR_TYPES.begin(), SIM_SENSOR_TYPES.end(), sensorType) != SIM_SENSOR_TYPES.end();
}

int64_t SensorSimVdi::NowNs()
{
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
}

void SensorSimVdi::FillSensorInfo(int32_t sensorType, HdfSensorInformationVdi& info)
{
    auto it = SIM_SAMPLES.find(sensorType);
    uint32_t dimension = (it != SIM_SAMPLES.end()) ? it->second.dimension : 1;

    info.sensorName = "virt_sensor_" + std::to_string(sensorType);
    info.vendorName = "virt";
    info.firmwareVersion = "1.0";
    info.hardwareVersion = "1.0";
    info.sensorTypeId = sensorType;
    info.sensorId = sensorType;
    info.sensorHandle.deviceId = 0;
    info.sensorHandle.sensorType = sensorType;
    info.sensorHandle.sensorId = sensorType;
    info.sensorHandle.location = 0;
    info.maxRange = 100.0f * dimension;
    info.accuracy = 0.5f;
    info.power = 0.1f;
    info.minDelay = MIN_DELAY_NS;
    info.maxDelay = MAX_DELAY_NS;
    info.fifoMaxEventCount = FIFO_MAX_EVENT_COUNT;
    info.reserved = 0;
}

void SensorSimVdi::FillSampleData(int32_t sensorType, std::vector<uint8_t>& data)
{
    auto it = SIM_SAMPLES.find(sensorType);
    if (it == SIM_SAMPLES.end()) {
        float fallback = 0.0f;
        data.resize(sizeof(float));
        if (memcpy_s(data.data(), data.size(), &fallback, sizeof(float)) == EOK) {
            return;
        }
        data.clear();
        return;
    }
    data.resize(it->second.dimension * sizeof(float));
    if (memcpy_s(data.data(), data.size(), it->second.values, data.size()) != EOK) {
        data.clear();
    }
}

SensorSimVdi::~SensorSimVdi()
{
    running_ = false;
    if (worker_.joinable()) {
        worker_.join();
    }
}

int32_t SensorSimVdi::Init()
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto type : SIM_SENSOR_TYPES) {
        SimSensorState state = {};
        sensors_[type] = state;
    }
    if (!running_) {
        running_ = true;
        worker_ = std::thread(&SensorSimVdi::Loop, this);
    }
    return HDF_SUCCESS;
}

int32_t SensorSimVdi::GetAllSensorInfo(std::vector<HdfSensorInformationVdi>& info)
{
    for (auto type : SIM_SENSOR_TYPES) {
        HdfSensorInformationVdi item = {};
        FillSensorInfo(type, item);
        info.push_back(item);
    }
    return HDF_SUCCESS;
}

int32_t SensorSimVdi::Enable(SensorHandle sensorHandle)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sensors_.find(sensorHandle.sensorType);
    if (it == sensors_.end()) {
        return SENSOR_NOT_SUPPORT;
    }
    it->second.enabled = true;
    it->second.handle = sensorHandle;
    it->second.lastReportNs = 0; /* report immediately after enabled */
    return HDF_SUCCESS;
}

int32_t SensorSimVdi::Disable(SensorHandle sensorHandle)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sensors_.find(sensorHandle.sensorType);
    if (it == sensors_.end()) {
        return SENSOR_NOT_SUPPORT;
    }
    it->second.enabled = false;
    return HDF_SUCCESS;
}

int32_t SensorSimVdi::SetBatch(SensorHandle sensorHandle, int64_t samplingInterval, int64_t reportInterval)
{
    (void)reportInterval;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sensors_.find(sensorHandle.sensorType);
    if (it == sensors_.end()) {
        return SENSOR_NOT_SUPPORT;
    }
    if (samplingInterval < 0) {
        return SENSOR_INVALID_PARAM;
    }
    if (samplingInterval > 0) {
        it->second.samplingIntervalNs = std::min(std::max(samplingInterval, minSamplingIntervalNs),
            maxSamplingIntervalNs);
    }
    return HDF_SUCCESS;
}

int32_t SensorSimVdi::SetMode(SensorHandle sensorHandle, int32_t mode)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sensors_.find(sensorHandle.sensorType);
    if (it == sensors_.end()) {
        return SENSOR_NOT_SUPPORT;
    }
    /*
     * Same mode contract as real sensor drivers: continuous sensors support
     * real-time reporting, the hall switch sensor also supports on-change
     * reporting; SENSOR_MODE_DEFAULT is not a valid report mode.
     */
    if (mode == SENSOR_MODE_REALTIME) {
        return SENSOR_SUCCESS;
    }
    if ((mode == SENSOR_MODE_ON_CHANGE) && (sensorHandle.sensorType == SENSOR_TYPE_HALL)) {
        return SENSOR_SUCCESS;
    }
    return SENSOR_FAILURE;
}

int32_t SensorSimVdi::SetOption(SensorHandle sensorHandle, uint32_t option)
{
    (void)option;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!IsSupportedType(sensorHandle.sensorType)) {
        return SENSOR_NOT_SUPPORT;
    }
    return HDF_SUCCESS;
}

int32_t SensorSimVdi::SetSaBatch(SensorHandle sensorHandle, int64_t samplingInterval, int64_t reportInterval)
{
    return SetBatch(sensorHandle, samplingInterval, reportInterval);
}

int32_t SensorSimVdi::GetDeviceSensorInfo(int32_t deviceId, std::vector<HdfSensorInformationVdi>& info)
{
    (void)deviceId;
    return GetAllSensorInfo(info);
}

int32_t SensorSimVdi::GetSdcSensorInfo(std::vector<SdcSensorInfoVdi>& sdcSensorInfo)
{
    (void)sdcSensorInfo;
    return HDF_SUCCESS;
}

int32_t SensorSimVdi::RegSensorPlugCallBack(const sptr<ISensorPlugCallback>& callbackObj)
{
    (void)callbackObj;
    return HDF_SUCCESS;
}

int32_t SensorSimVdi::UnRegSensorPlugCallBack(const sptr<ISensorPlugCallback>& callbackObj)
{
    (void)callbackObj;
    return HDF_SUCCESS;
}

int32_t SensorSimVdi::Enable(int32_t sensorId)
{
    return Enable(MakeHandle(0, sensorId, 0, 0));
}

int32_t SensorSimVdi::Disable(int32_t sensorId)
{
    return Disable(MakeHandle(0, sensorId, 0, 0));
}

int32_t SensorSimVdi::SetBatch(int32_t sensorId, int64_t samplingInterval, int64_t reportInterval)
{
    return SetBatch(MakeHandle(0, sensorId, 0, 0), samplingInterval, reportInterval);
}

int32_t SensorSimVdi::SetMode(int32_t sensorId, int32_t mode)
{
    return SetMode(MakeHandle(0, sensorId, 0, 0), mode);
}

int32_t SensorSimVdi::SetOption(int32_t sensorId, uint32_t option)
{
    return SetOption(MakeHandle(0, sensorId, 0, 0), option);
}

int32_t SensorSimVdi::SetSaBatch(int32_t sensorId, int64_t samplingInterval, int64_t reportInterval)
{
    return SetSaBatch(MakeHandle(0, sensorId, 0, 0), samplingInterval, reportInterval);
}

int32_t SensorSimVdi::Register(int32_t groupId, const sptr<ISensorCallbackVdi>& callbackObj)
{
    if ((groupId != TRADITIONAL_SENSOR_TYPE) && (groupId != MEDICAL_SENSOR_TYPE)) {
        return SENSOR_INVALID_PARAM;
    }
    if (callbackObj == nullptr) {
        return SENSOR_INVALID_PARAM;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto& list = callbacks_[groupId];
    for (auto& cb : list) {
        if (cb.GetRefPtr() == callbackObj.GetRefPtr()) {
            return HDF_SUCCESS;
        }
    }
    list.push_back(callbackObj);
    return HDF_SUCCESS;
}

int32_t SensorSimVdi::Unregister(int32_t groupId, const sptr<ISensorCallbackVdi>& callbackObj)
{
    if ((groupId != TRADITIONAL_SENSOR_TYPE) && (groupId != MEDICAL_SENSOR_TYPE)) {
        return SENSOR_INVALID_PARAM;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = callbacks_.find(groupId);
    if (it == callbacks_.end()) {
        return HDF_SUCCESS;
    }
    auto& list = it->second;
    for (auto iter = list.begin(); iter != list.end(); ++iter) {
        if (iter->GetRefPtr() == callbackObj.GetRefPtr()) {
            list.erase(iter);
            break;
        }
    }
    return HDF_SUCCESS;
}

void SensorSimVdi::DispatchEvent(int32_t sensorType, const SimSensorState& state)
{
    HdfSensorEventsVdi event = {};
    event.sensorId = sensorType;
    event.version = 1;
    event.timestamp = NowNs();
    event.option = 0;
    event.mode = 0;
    event.deviceSensorInfo.deviceId = state.handle.deviceId;
    event.deviceSensorInfo.sensorType = sensorType;
    event.deviceSensorInfo.sensorId = state.handle.sensorId;
    event.deviceSensorInfo.location = state.handle.location;
    FillSampleData(sensorType, event.data);
    event.dataLen = event.data.size();

    std::vector<sptr<ISensorCallbackVdi>> targets;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = callbacks_.find(TRADITIONAL_SENSOR_TYPE);
        if (it == callbacks_.end() || it->second.empty()) {
            return;
        }
        targets = it->second;
    }
    for (auto& cb : targets) {
        if (cb != nullptr) {
            cb->OnDataEventVdi(event);
        }
    }
}

void SensorSimVdi::Loop()
{
    while (running_) {
        int64_t now = NowNs();
        std::vector<std::pair<int32_t, SimSensorState>> due;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& [type, state] : sensors_) {
                if (!state.enabled) {
                    continue;
                }
                if ((state.lastReportNs == 0) || (now - state.lastReportNs >= state.samplingIntervalNs)) {
                    state.lastReportNs = now;
                    due.emplace_back(type, state);
                }
            }
        }
        for (auto& [type, state] : due) {
            DispatchEvent(type, state);
        }
        std::this_thread::sleep_for(std::chrono::nanoseconds(pollPeriodNs));
    }
}

} // namespace V1_1
} // namespace Sensor
} // namespace HDI
} // namespace OHOS

namespace OHOS {
namespace HDI {
namespace Sensor {
namespace V1_1 {

static int32_t CreateSensorSimVdiInstance(struct HdfVdiBase *vdiBase)
{
    if (vdiBase == nullptr) {
        return HDF_FAILURE;
    }
    struct WrapperSensorVdi *sensorVdi = reinterpret_cast<struct WrapperSensorVdi *>(vdiBase);
    sensorVdi->sensorModule = new SensorSimVdi();
    if (sensorVdi->sensorModule == nullptr) {
        return HDF_FAILURE;
    }
    return HDF_SUCCESS;
}

static int32_t DestorySensorSimVdiInstance(struct HdfVdiBase *vdiBase)
{
    if (vdiBase == nullptr) {
        return HDF_FAILURE;
    }
    struct WrapperSensorVdi *sensorVdi = reinterpret_cast<struct WrapperSensorVdi *>(vdiBase);
    SensorSimVdi *impl = reinterpret_cast<SensorSimVdi *>(sensorVdi->sensorModule);
    if (impl != nullptr) {
        delete impl;
        sensorVdi->sensorModule = nullptr;
    }
    return HDF_SUCCESS;
}

static struct WrapperSensorVdi g_sensorSimVdi = {
    .base = {
        .moduleVersion = 1,
        .moduleName = "sensor_Service",
        .CreateVdiInstance = CreateSensorSimVdiInstance,
        .DestoryVdiInstance = DestorySensorSimVdiInstance,
    },
    .sensorModule = nullptr,
};

} // namespace V1_1
} // namespace Sensor
} // namespace HDI
} // namespace OHOS

extern "C" HDF_VDI_INIT(OHOS::HDI::Sensor::V1_1::g_sensorSimVdi);
