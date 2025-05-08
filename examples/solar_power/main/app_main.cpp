/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>

#include <esp_matter.h>
#include <esp_matter_console.h>

#include <common_macros.h>
#include <app_priv.h>
#include <app_reset.h>
#include <app/clusters/electrical-energy-measurement-server/electrical-energy-measurement-server.h>
#include <app/clusters/electrical-power-measurement-server/electrical-power-measurement-server.h>
#include <static-supported-temperature-levels.h>
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ESP32/OpenthreadLauncher.h>
#endif

static const char *TAG = "app_main";
static uint16_t solar_power_endpoint_id = 0;

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using namespace chip::app::Clusters::ElectricalEnergyMeasurement;
using namespace chip::app::Clusters::ElectricalEnergyMeasurement::Attributes;
using namespace chip::app::Clusters::ElectricalEnergyMeasurement::Structs;
using namespace chip::app::DataModel;

static chip::app::Clusters::TemperatureControl::AppSupportedTemperatureLevelsDelegate sAppSupportedTemperatureLevelsDelegate;
static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "Interface IP Address Changed");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        break;

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Commissioning failed, fail safe timer expired");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStarted:
        ESP_LOGI(TAG, "Commissioning session started");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStopped:
        ESP_LOGI(TAG, "Commissioning session stopped");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Commissioning window opened");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Commissioning window closed");
        break;

    default:
        break;
    }
}

// This callback is invoked when clients interact with the Identify Cluster.
// In the callback implementation, an endpoint can identify itself. (e.g., by flashing an LED or light).
static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback: type: %u, effect: %u, variant: %u", type, effect_id, effect_variant);
    return ESP_OK;
}

// This callback is called for every attribute update. The callback implementation shall
// handle the desired attributes and return an appropriate error code. If the attribute
// is not of your interest, please do not return an error code and strictly return ESP_OK.
static esp_err_t app_attribute_update_cb(callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    if (type == PRE_UPDATE) {
        /* Handle the attribute updates here. */
    }

    return ESP_OK;
}

extern "C" void app_main()
{
    esp_err_t err = ESP_OK;
    nullable<int64_t> active_power = 0;

    /* Initialize the ESP NVS layer */
    nvs_flash_init();

    /* Initialize driver */
    app_driver_handle_t reset_handle = app_driver_button_init();
    app_reset_button_register(reset_handle);

    /* Create a Matter node and add the mandatory Root Node device type on endpoint 0 */
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

    solar_power::config_t solar_power_config;
    static chip::app::Clusters::ElectricalPowerMeasurement::Delegate epm_delegate;
    solar_power_config.delegate = epm_delegate;
    endpoint_t *endpoint = solar_power::create(node, &solar_power_config, ENDPOINT_FLAG_NONE, NULL);
    ABORT_APP_ON_FAILURE(endpoint != nullptr, ESP_LOGE(TAG, "Failed to create solar power endpoint"));

    cluster_t *electrical_power_measurement_cluster = cluster::get(endpoint, chip::app::Clusters::ElectricalPowerMeasurement::Id);
    cluster::electrical_power_measurement::attribute::create_active_power(electrical_power_measurement_cluster, active_power);

    solar_power_endpoint_id = endpoint::get_id(endpoint);
    ESP_LOGI(TAG, "Solar Power created with endpoint_id %d", solar_power_endpoint_id);

    device_energy_management::config_t device_energy_management_config;
    device_energy_management::add(endpoint, &device_energy_management_config);

    cluster_t *device_energy_management_cluster = cluster::get(endpoint, chip::app::Clusters::DeviceEnergyManagement::Id);
    cluster::device_energy_management::feature::power_adjustment::add(device_energy_management_cluster);

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    /* Set OpenThread platform config */
    esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&config);
#endif

    /* Matter start */
    err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));

    /* Update ElectricalPowerMeasurement values */
    esp_matter_attr_val_t active_current_val_t  = esp_matter_int64(2 * 1000);
    esp_matter_attr_val_t active_power_val_t    = esp_matter_int64(20 * 1000);
    esp_matter_attr_val_t voltage_val_t         = esp_matter_int64(230 * 1000);
    esp_matter_attr_val_t mes_types_val_t       = esp_matter_uint8(3);

    esp_matter::attribute::update(solar_power_endpoint_id, ElectricalPowerMeasurement::Id, ElectricalPowerMeasurement::Attributes::ActiveCurrent::Id, &active_current_val_t);
    esp_matter::attribute::update(solar_power_endpoint_id, ElectricalPowerMeasurement::Id, ElectricalPowerMeasurement::Attributes::ActivePower::Id, &active_power_val_t);
    esp_matter::attribute::update(solar_power_endpoint_id, ElectricalPowerMeasurement::Id, ElectricalPowerMeasurement::Attributes::Voltage::Id, &voltage_val_t);
    esp_matter::attribute::update(solar_power_endpoint_id, ElectricalPowerMeasurement::Id, ElectricalPowerMeasurement::Attributes::NumberOfMeasurementTypes::Id, &mes_types_val_t);

    // SetMeasurementAccuracy
	auto mask = chip::BitMask<chip::app::Clusters::ElectricalEnergyMeasurement::Feature>(
                chip::app::Clusters::ElectricalEnergyMeasurement::Feature::kCumulativeEnergy, 
                chip::app::Clusters::ElectricalEnergyMeasurement::Feature::kExportedEnergy);
	auto optionalmask = chip::BitMask<chip::app::Clusters::ElectricalEnergyMeasurement::OptionalAttributes>(
        chip::app::Clusters::ElectricalEnergyMeasurement::OptionalAttributes::kOptionalAttributeCumulativeEnergyReset);
	auto energyMeasurementAccess = new chip::app::Clusters::ElectricalEnergyMeasurement::ElectricalEnergyMeasurementAttrAccess(mask, optionalmask);

	auto initerr = energyMeasurementAccess->Init();
	if (chip::ChipError::IsSuccess(initerr) == false) {
		ESP_LOGE(TAG, "energyMeasurementAccess->Init() ERR");
	}


    // https://github.com/project-chip/connectedhomeip/blob/master/examples/energy-management-app/energy-management-common/common/src/EnergyManagementAppCommonMain.cpp
    /* MeasurementAccuracyRangeStruct */
    const MeasurementAccuracyRangeStruct::Type activePowerAccuracyRanges[] = {
        // 2 - 5%, 3% Typ
        {
            .rangeMin       = -50'000'000, // -50kW
            .rangeMax       = -10'000'000, // -10kW
            .percentMax     = MakeOptional(static_cast<chip::Percent100ths>(5000)),
            .percentMin     = MakeOptional(static_cast<chip::Percent100ths>(2000)),
            .percentTypical = MakeOptional(static_cast<chip::Percent100ths>(3000)),
        },
        // 0.1 - 1%, 0.5% Typ
        {
            .rangeMin       = -9'999'999, // -9.999kW
            .rangeMax       = 9'999'999,  //  9.999kW
            .percentMax     = MakeOptional(static_cast<chip::Percent100ths>(1000)),
            .percentMin     = MakeOptional(static_cast<chip::Percent100ths>(100)),
            .percentTypical = MakeOptional(static_cast<chip::Percent100ths>(500)),
        },
        // 2 - 5%, 3% Typ
        {
            .rangeMin       = 10'000'000, // 10 kW
            .rangeMax       = 50'000'000, // 50 kW
            .percentMax     = MakeOptional(static_cast<chip::Percent100ths>(5000)),
            .percentMin     = MakeOptional(static_cast<chip::Percent100ths>(2000)),
            .percentTypical = MakeOptional(static_cast<chip::Percent100ths>(3000)),
        },
    };
    
    /* MeasurementAccuracyRangeStruct */
    const MeasurementAccuracyRangeStruct::Type activeCurrentAccuracyRanges[] = {
        // 2 - 5%, 3% Typ
        {
            .rangeMin       = -100'000, // -100A
            .rangeMax       = -5'000,   // -5A
            .percentMax     = MakeOptional(static_cast<chip::Percent100ths>(5000)),
            .percentMin     = MakeOptional(static_cast<chip::Percent100ths>(2000)),
            .percentTypical = MakeOptional(static_cast<chip::Percent100ths>(3000)),
        },
        // 0.1 - 1%, 0.5% Typ
        {
            .rangeMin       = -4'999, // -4.999A
            .rangeMax       = 4'999,  //  4.999A
            .percentMax     = MakeOptional(static_cast<chip::Percent100ths>(1000)),
            .percentMin     = MakeOptional(static_cast<chip::Percent100ths>(100)),
            .percentTypical = MakeOptional(static_cast<chip::Percent100ths>(500)),
        },
        // 2 - 5%, 3% Typ
        {
            .rangeMin       = 5'000,   // 5A
            .rangeMax       = 100'000, // 100 A
            .percentMax     = MakeOptional(static_cast<chip::Percent100ths>(5000)),
            .percentMin     = MakeOptional(static_cast<chip::Percent100ths>(2000)),
            .percentTypical = MakeOptional(static_cast<chip::Percent100ths>(3000)),
        },
    };
    
    /* MeasurementAccuracyRangeStruct */
    const MeasurementAccuracyRangeStruct::Type voltageAccuracyRanges[] = {
        // 2 - 5%, 3% Typ
        {
            .rangeMin       = -500'000, // -500V
            .rangeMax       = -100'000, // -100V
            .percentMax     = MakeOptional(static_cast<chip::Percent100ths>(5000)),
            .percentMin     = MakeOptional(static_cast<chip::Percent100ths>(2000)),
            .percentTypical = MakeOptional(static_cast<chip::Percent100ths>(3000)),
        },
        // 0.1 - 1%, 0.5% Typ
        {
            .rangeMin       = -99'999, // -99.999V
            .rangeMax       = 99'999,  //  99.999V
            .percentMax     = MakeOptional(static_cast<chip::Percent100ths>(1000)),
            .percentMin     = MakeOptional(static_cast<chip::Percent100ths>(100)),
            .percentTypical = MakeOptional(static_cast<chip::Percent100ths>(500)),
        },
        // 2 - 5%, 3% Typ
        {
            .rangeMin       = 100'000, // 100 V
            .rangeMax       = 500'000, // 500 V
            .percentMax     = MakeOptional(static_cast<chip::Percent100ths>(5000)),
            .percentMin     = MakeOptional(static_cast<chip::Percent100ths>(2000)),
            .percentTypical = MakeOptional(static_cast<chip::Percent100ths>(3000)),
        }
    };
    
    /* MeasurementAccuracyStruct */
    static const Structs::MeasurementAccuracyStruct::Type kMeasurementAccuracies[] = {
        {
            .measurementType  = chip::app::Clusters::detail::MeasurementTypeEnum::kActivePower,
            .measured         = true,
            .minMeasuredValue = -50'000'000, // -50 kW
            .maxMeasuredValue = 50'000'000,  //  50 kW
            .accuracyRanges   = DataModel::List<const MeasurementAccuracyRangeStruct::Type>(activePowerAccuracyRanges),
        },
        {
            .measurementType  = chip::app::Clusters::detail::MeasurementTypeEnum::kActiveCurrent,
            .measured         = true,
            .minMeasuredValue = -100'000, // -100A
            .maxMeasuredValue = 100'000,  //  100A
            .accuracyRanges   = DataModel::List<const MeasurementAccuracyRangeStruct::Type>(activeCurrentAccuracyRanges),
        },
        {
            .measurementType  = chip::app::Clusters::detail::MeasurementTypeEnum::kVoltage,
            .measured         = true,
            .minMeasuredValue = -500'000, // -500V
            .maxMeasuredValue = 500'000,  //  500V
            .accuracyRanges   = DataModel::List<const MeasurementAccuracyRangeStruct::Type>(voltageAccuracyRanges),
        },
    };

    // List[MeasurementAccuracyRangeStruct]
    DataModel::List<const MeasurementAccuracyStruct::Type> kMeasurementAccuraciesList;

    // Example of setting CumulativeEnergyReset structure - for now set these to 0
    // but the manufacturer may want to store these in non volatile storage for timestamp (based on epoch_s)
    // Create an accuracy entry which is between +/-0.5 and +/- 5% across the range of all possible energy readings
    // https://github.com/project-chip/connectedhomeip/blob/master/examples/energy-management-app/energy-management-common/common/src/EnergyManagementAppCommonMain.cpp
    static const MeasurementAccuracyRangeStruct::Type kEnergyAccuracyRanges[] = {
        { 
            .rangeMin   = 0,
            .rangeMax   = 1'000'000'000'000'000, // 1 million Mwh
            .percentMax = MakeOptional(static_cast<chip::Percent100ths>(500)),
            .percentMin = MakeOptional(static_cast<chip::Percent100ths>(50)) 
        }
    };

    static const MeasurementAccuracyStruct::Type kEnergyMeasurementAccuracy = {
        .measurementType  = chip::app::Clusters::detail::MeasurementTypeEnum::kElectricalEnergy,
        .measured         = true, // this should be made true in an implementation where a real metering device is used.
        .minMeasuredValue = 0,
        .maxMeasuredValue = 1'000'000'000'000'000, // 1 million Mwh
        .accuracyRanges = DataModel::List<const MeasurementAccuracyRangeStruct::Type>(kEnergyAccuracyRanges)
    };

    lock::chip_stack_lock(portMAX_DELAY);
    /* ElectricalEnergyMeasurement::SetMeasurementAccuracy */
    auto err_accu = chip::app::Clusters::ElectricalEnergyMeasurement::SetMeasurementAccuracy(solar_power_endpoint_id, kEnergyMeasurementAccuracy);
	if (chip::ChipError::IsSuccess(err_accu) == false) { 
        ESP_LOGI(TAG, "ElectricalEnergyMeasurement SetMeasurementAccuracy ERR");
	}

    /* Update CumulativeEnergy */
    int energy_value = 10000;
    //int endpoint_id = 1;
    
    chip::app::Clusters::ElectricalEnergyMeasurement::Structs::EnergyMeasurementStruct::Type energyExported;
    energyExported.startTimestamp.SetValue(1746380384); // Change to your marked start EPOCH time of energy
    energyExported.endTimestamp.SetValue(1746386384); // Change to your own API to get EPOCH time (Or use systime APIs)
    energyExported.energy = energy_value;
    
    // NotifyCumulativeEnergyMeasured(aEndpointId, MakeOptional(energyImported), MakeOptional(energyExported))
    auto success = chip::app::Clusters::ElectricalEnergyMeasurement::NotifyCumulativeEnergyMeasured(solar_power_endpoint_id, 
                   {},
                   chip::Optional<chip::app::Clusters::ElectricalEnergyMeasurement::Structs::EnergyMeasurementStruct::Type> (energyExported));
    ESP_LOGI(TAG, "NotifyCumulativeEnergyMeasured()");
    lock::chip_stack_unlock();


#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
    esp_matter::console::factoryreset_register_commands();
    esp_matter::console::init();
#endif
}
