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

#ifndef SENSOR_SIM_VDI_H
#define SENSOR_SIM_VDI_H

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "isensor_callback_vdi.h"
#include "isensor_interface_vdi.h"

namespace OHOS {
namespace HDI {
namespace Sensor {
namespace V1_1 {

/*
 * Simulated sensor VDI for the virtual (qemu) product.
 *
 * The virtual device has no physical sensors, so this product VDI provides a
 * static list of standard sensors and generates plausible data events for
 * every enabled sensor at the requested sampling interval. It is loaded by
 * sensor_host through HDI_SENSOR_PRODUCT_VDI_LIBNAME
 * ("libhdi_product_sensor_impl.z.so").
 */
class SensorSimVdi : public ISensorInterfaceVdi {
public:
    SensorSimVdi() = default;
    ~SensorSimVdi() override;

    int32_t Init() override;
    int32_t GetAllSensorInfo(std::vector<HdfSensorInformationVdi>& info) override;

    /* V1_1 int32_t (sensorId based) entry points */
    int32_t Enable(int32_t sensorId) override;
    int32_t Disable(int32_t sensorId) override;
    int32_t SetBatch(int32_t sensorId, int64_t samplingInterval, int64_t reportInterval) override;
    int32_t SetMode(int32_t sensorId, int32_t mode) override;
    int32_t SetOption(int32_t sensorId, uint32_t option) override;
    int32_t SetSaBatch(int32_t sensorId, int64_t samplingInterval, int64_t reportInterval) override;

    /* V3_0 SensorHandle based entry points */
    int32_t Enable(SensorHandle sensorHandle) override;
    int32_t Disable(SensorHandle sensorHandle) override;
    int32_t SetBatch(SensorHandle sensorHandle, int64_t samplingInterval, int64_t reportInterval) override;
    int32_t SetMode(SensorHandle sensorHandle, int32_t mode) override;
    int32_t SetOption(SensorHandle sensorHandle, uint32_t option) override;
    int32_t SetSaBatch(SensorHandle sensorHandle, int64_t samplingInterval, int64_t reportInterval) override;
    int32_t GetDeviceSensorInfo(int32_t deviceId, std::vector<HdfSensorInformationVdi>& info) override;
    int32_t GetSdcSensorInfo(std::vector<SdcSensorInfoVdi>& sdcSensorInfo) override;
    int32_t RegSensorPlugCallBack(const sptr<ISensorPlugCallback>& callbackObj) override;
    int32_t UnRegSensorPlugCallBack(const sptr<ISensorPlugCallback>& callbackObj) override;

    int32_t Register(int32_t groupId, const sptr<ISensorCallbackVdi>& callbackObj) override;
    int32_t Unregister(int32_t groupId, const sptr<ISensorCallbackVdi>& callbackObj) override;

private:
    struct SimSensorState {
        bool enabled = false;
        int64_t samplingIntervalNs = defaultSamplingIntervalNs;
        int64_t lastReportNs = 0;
        SensorHandle handle = {};
    };

    static constexpr int64_t defaultSamplingIntervalNs = 200000000; /* 200 ms */
    static constexpr int64_t minSamplingIntervalNs = 1000000;       /* 1 ms */
    static constexpr int64_t maxSamplingIntervalNs = 10000000000;   /* 10 s */
    static constexpr int64_t pollPeriodNs = 5000000;                /* 5 ms */

    static const std::vector<int32_t>& GetSupportedTypes();
    static void FillSensorInfo(int32_t sensorType, HdfSensorInformationVdi& info);
    static void FillSampleData(int32_t sensorType, std::vector<uint8_t>& data);
    static bool IsSupportedType(int32_t sensorType);
    static int64_t NowNs();

    void Loop();
    void DispatchEvent(int32_t sensorType, const SimSensorState& state);

    std::mutex mutex_;
    std::map<int32_t, SimSensorState> sensors_;  /* key: sensorType */
    std::map<int32_t, std::vector<sptr<ISensorCallbackVdi>>> callbacks_; /* key: groupId */
    std::thread worker_;
    std::atomic<bool> running_ { false };
};

} // namespace V1_1
} // namespace Sensor
} // namespace HDI
} // namespace OHOS

#endif // SENSOR_SIM_VDI_H
