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
#include <static-supported-temperature-levels.h>
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ESP32/OpenthreadLauncher.h>
#endif

static const char *TAG = "app_main";
static uint16_t solar_power_endpoint_id = 0;
//static uint16_t temp_ctrl_endpoint_id = 0;

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;

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

    // "Identify", "Groups", "Scenes", "Refrigerator Mode Select" and "Refrigerator Alarm" are optional cluster for refrigerator device type so we are not adding them by default.
    //refrigerator::config_t refrigerator_config;
    solar_power::config_t solar_power_config;
    endpoint_t *endpoint = solar_power::create(node, &solar_power_config, ENDPOINT_FLAG_NONE, NULL);
    ABORT_APP_ON_FAILURE(endpoint != nullptr, ESP_LOGE(TAG, "Failed to create refrigerator endpoint"));

    cluster_t *electrical_power_measurement_cluster = cluster::get(endpoint, chip::app::Clusters::ElectricalPowerMeasurement::Id);
    cluster::electrical_power_measurement::attribute::create_active_power(electrical_power_measurement_cluster, active_power);

    solar_power_endpoint_id = endpoint::get_id(endpoint);
    ESP_LOGI(TAG, "Solar Power created with endpoint_id %d", solar_power_endpoint_id);

    device_energy_management::config_t device_energy_management_config;
    device_energy_management::add(endpoint, &device_energy_management_config);

    cluster_t *device_energy_management_cluster = cluster::get(endpoint, chip::app::Clusters::DeviceEnergyManagement::Id);
    cluster::device_energy_management::feature::power_adjustment::add(device_energy_management_cluster);

    /* EEM 
    electrical_energy_measurement::config_t econfig;
    auto cumulative_energy_feature = electrical_energy_measurement::feature::cumulative_energy::get_id();
    auto imported_energy_feature = electrical_energy_measurement::feature::imported_energy::get_id();
    esp_matter::cluster_t *energy_cluster = electrical_energy_measurement::create(electrical_sensor_endpoint, &econfig, CLUSTER_FLAG_SERVER,
        cumulative_energy_feature | imported_energy_feature);
    */

    /*
    // "Temperature Measurement", "Refrigerator and Temperature Controlled Cabinet Mode Select" are optional cluster for temperature_controlled_cabinet device type so we are not adding them by default.
    temperature_controlled_cabinet::config_t temperature_controlled_cabinet_config;
    endpoint_t *endpoint1 = temperature_controlled_cabinet::create(node, &temperature_controlled_cabinet_config, ENDPOINT_FLAG_NONE, NULL);
    ABORT_APP_ON_FAILURE(endpoint1 != nullptr, ESP_LOGE(TAG, "Failed to create temperature controlled cabinet endpoint"));

    esp_matter::cluster_t *cluster = esp_matter::cluster::get(endpoint1, chip::app::Clusters::TemperatureControl::Id);

    // At lest one of temperature_number and temperature_level feature is mandatory.
    cluster::temperature_control::feature::temperature_number::config_t temperature_number_config;
    cluster::temperature_control::feature::temperature_number::add(cluster, &temperature_number_config);

    temp_ctrl_endpoint_id = endpoint::get_id(endpoint1);
    ESP_LOGI(TAG, "Temperature controlled cabinet created with endpoint_id %d", temp_ctrl_endpoint_id);

    err = set_parent_endpoint(endpoint1, endpoint);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to set parent endpoint, err:%d", err));
    */

    /* add device_energy_management device type to main EP */
    //add_device_type(endpoint, endpoint::device_energy_management::get_device_type_id, endpoint::device_energy_management::get_device_type_version);

    /* add electrical_sensor device type to main EP */
    //add_device_type(endpoint, cluster::electrical_sensor::get_device_type_id, cluster::electrical_sensor::get_device_type_version);

    /* add power_source device type to main EP */
    //add_device_type(endpoint, cluster::power_source_device::get_device_type_id, cluster::power_source_device::get_device_type_version);

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

    // SetMeasurementAccuracy

	auto mask = chip::BitMask<chip::app::Clusters::ElectricalEnergyMeasurement::Feature>(
        chip::app::Clusters::ElectricalEnergyMeasurement::Feature::kCumulativeEnergy, 
        chip::app::Clusters::ElectricalEnergyMeasurement::Feature::kImportedEnergy);
	auto optionalmask = chip::BitMask<chip::app::Clusters::ElectricalEnergyMeasurement::OptionalAttributes>(
        chip::app::Clusters::ElectricalEnergyMeasurement::OptionalAttributes::kOptionalAttributeCumulativeEnergyReset);
	auto energyMeasurementAccess = new chip::app::Clusters::ElectricalEnergyMeasurement::ElectricalEnergyMeasurementAttrAccess(mask,optionalmask);

	auto initerr = energyMeasurementAccess->Init();
	if (chip::ChipError::IsSuccess(initerr) == false) {
		ESP_LOGE(TAG, "energyMeasurementAccess->Init() ERR");
	}
	
    int max_energy = 100;
	chip::app::Clusters::ElectricalEnergyMeasurement::Structs::MeasurementAccuracyStruct::Type measurementAccuracy;
	static chip::app::Clusters::ElectricalEnergyMeasurement::Structs::MeasurementAccuracyRangeStruct::Type sMeasurementAccuracyRange;

	measurementAccuracy.measured = true;
	measurementAccuracy.measurementType = chip::app::Clusters::detail::MeasurementTypeEnum::kElectricalEnergy;
	measurementAccuracy.maxMeasuredValue = max_energy;
	measurementAccuracy.minMeasuredValue = 0;

	sMeasurementAccuracyRange.rangeMax = max_energy;
	sMeasurementAccuracyRange.rangeMin = 0;
	// sMeasurementAccuracyRange.percentMax.SetValue(energy_max_accuracy);
	// sMeasurementAccuracyRange.percentMin.SetValue(energy_min_accuracy);
	// sMeasurementAccuracyRange.percentTypical.SetValue(energy_typ_accuracy);
	sMeasurementAccuracyRange.fixedMax.SetValue(187650443764698);
	sMeasurementAccuracyRange.fixedMin.SetValue(187650443764696);
	sMeasurementAccuracyRange.fixedTypical.SetValue(0);

	measurementAccuracy.accuracyRanges = {sMeasurementAccuracyRange};

    auto err_accu = chip::app::Clusters::ElectricalEnergyMeasurement::SetMeasurementAccuracy(solar_power_endpoint_id, measurementAccuracy);
	if (chip::ChipError::IsSuccess(err_accu) == false) { 
		//logger.E("SetMeasurementAccuracy ERR %u %u", err_accu.GetRange(), err_accu.GetValue()); // Change to log_e or your own logger
        ESP_LOGI(TAG, "SetMeasurementAccuracy ERR");
	}

    int energy_value = 10000;
    int endpoint_id = 1;
    chip::app::Clusters::ElectricalEnergyMeasurement::Structs::EnergyMeasurementStruct::Type measurement;
    measurement.startTimestamp.SetValue(1746386384-3600); // Change to your marked start EPOCH time of energy
    measurement.endTimestamp.SetValue(1746386384); // Change to your own API to get EPOCH time (Or use systime APIs)
    measurement.energy = energy_value;
    
    chip::DeviceLayer::StackLock lock;
    auto success = chip::app::Clusters::ElectricalEnergyMeasurement::NotifyCumulativeEnergyMeasured(endpoint_id, {}, chip::Optional<chip::app::Clusters::ElectricalEnergyMeasurement::Structs::EnergyMeasurementStruct::Type> (measurement));
    ESP_LOGI(TAG, "NotifyCumulativeEnergyMeasured() -> %u", success);

    //chip::app::Clusters::TemperatureControl::SetInstance(&sAppSupportedTemperatureLevelsDelegate);

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
    esp_matter::console::factoryreset_register_commands();
    esp_matter::console::init();
#endif
}
