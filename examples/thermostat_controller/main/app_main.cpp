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
#include <esp_matter_client.h>
#include <esp_matter_console.h>
#include <esp_matter_ota.h>
#include <esp_matter_providers.h>

#include <app/clusters/bindings/binding-table.h>
#include <app/server/Server.h>

#include <common_macros.h>
#include <app_priv.h>
#include <app_reset.h>
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ESP32/OpenthreadLauncher.h>
#endif

static const char *TAG = "app_main";
uint16_t thermostat_controller_endpoint_id = 0;

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;

/* Once bound, we only need to subscribe to the remote thermostat's attributes a single
 * time. This is set back to true on every reboot: the binding table itself persists in
 * NVS across reboots, but the in-RAM subscription (ReadClient) does not, so it has to be
 * re-established on every boot, not just the first time the binding is written. */
static bool s_subscribe_pending = true;

/* Scan the (persisted) binding table for a bound Thermostat and, if found and not already
 * subscribed, connect to it and trigger app_driver_subscribe_thermostat_attributes() (via
 * the client request callback). Called both when the binding table changes live (fresh
 * `binding write`) and once connectivity comes up after boot, to also cover a binding that
 * was already present in NVS from a previous session. */
static void try_subscribe_to_bound_thermostat()
{
    if (!s_subscribe_pending) {
        return;
    }
    for (const auto &binding : chip::app::Clusters::Binding::Table::GetInstance()) {
        if (binding.type != chip::app::Clusters::Binding::MATTER_UNICAST_BINDING ||
            binding.clusterId.value_or(0) != chip::app::Clusters::Thermostat::Id) {
            continue;
        }
        ESP_LOGI(TAG, "Bound to thermostat nodeId=0x" ChipLogFormatX64 " endpoint=%d, subscribing to "
                "ThermostatRunningState/LocalTemperature/OccupiedHeatingSetpoint",
                ChipLogValueX64(binding.nodeId), binding.remote);

        /* Only the endpoint/cluster carried here matter: app_driver_subscribe_thermostat_attributes()
         * builds the actual (multi-attribute) subscription path list from them. */
        client::request_handle_t req_handle;
        req_handle.type = esp_matter::client::SUBSCRIBE_ATTR;
        req_handle.attribute_path = {binding.remote, binding.clusterId.value(),
                                     chip::app::Clusters::Thermostat::Attributes::ThermostatRunningState::Id};
        auto &server = chip::Server::GetInstance();
        client::connect(server.GetCASESessionManager(), binding.fabricIndex, binding.nodeId, &req_handle);
        s_subscribe_pending = false;
        break;
    }
}

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "Interface IP Address Changed");
        /* Covers the case where the device already had a bound thermostat in NVS before
         * this boot: the binding table itself is not "changed" on reboot, so
         * kBindingsChangedViaCluster below would never fire for it. */
        try_subscribe_to_bound_thermostat();
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

    case chip::DeviceLayer::DeviceEventType::kBindingsChangedViaCluster:
        ESP_LOGI(TAG, "Binding entry changed");
        try_subscribe_to_bound_thermostat();
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

    /* Initialize the ESP NVS layer */
    nvs_flash_init();

    /* Initialize driver: factory-reset button and heat-demand status LED */
    app_driver_handle_t driver_handle = app_driver_boiler_demand_init();
    app_reset_button_register(driver_handle);

    /* Create a Matter node and add the mandatory Root Node device type on endpoint 0 */
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

    /* Add the Thermostat Controller device type: a descriptor server plus a Thermostat
     * client cluster used to read/subscribe the state of a bound (real) thermostat. */
    thermostat_controller::config_t thermostat_controller_config;
    endpoint_t *endpoint =
        thermostat_controller::create(node, &thermostat_controller_config, ENDPOINT_FLAG_NONE, driver_handle);
    ABORT_APP_ON_FAILURE(endpoint != nullptr, ESP_LOGE(TAG, "Failed to create thermostat controller endpoint"));

    thermostat_controller_endpoint_id = endpoint::get_id(endpoint);
    ESP_LOGI(TAG, "Thermostat controller created with endpoint_id %d", thermostat_controller_endpoint_id);

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

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
    esp_matter::console::factoryreset_register_commands();
    esp_matter::console::init();
#endif
}
