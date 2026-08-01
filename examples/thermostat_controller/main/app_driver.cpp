/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_log.h>
#include <stdlib.h>
#include <string.h>

#include <esp_matter.h>
#include <esp_matter_client.h>
#include "bsp/esp-bsp.h"
#include <led_convert.h>

#include <app_priv.h>
#include <app_reset.h>

#include <app/server/Server.h>
#include <lib/core/Optional.h>

using namespace chip::app::Clusters;
using namespace esp_matter;

static const char *TAG = "app_driver";

static led_indicator_handle_t s_status_led = NULL;
static bool s_heat_demand_active = false;

/* This is where the actual boiler control would happen. For now this example only logs the
 * change and drives the on-board status LED so the behavior can be observed without any
 * extra wiring: swap the body of this function for a relay/opto-isolator GPIO drive once you
 * are ready to switch a real boiler. */
static void app_driver_set_heat_demand(bool active)
{
    if (active == s_heat_demand_active) {
        return;
    }
    s_heat_demand_active = active;

    if (active) {
        ESP_LOGI(TAG, "Le thermostat demande un cycle de chauffe -> chaudiere ON");
    } else {
        ESP_LOGI(TAG, "Plus de demande de chauffe -> chaudiere OFF");
    }

#if CONFIG_BSP_LEDS_NUM > 0
    if (s_status_led) {
        if (active) {
            led_indicator_irgb_t red = {};
            red.r = 255;
            red.g = 0;
            red.b = 0;
            red.index = 127; /* control all LEDs on the strip */
            led_indicator_set_rgb(s_status_led, red.value);
            led_indicator_start(s_status_led, BSP_LED_ON);
        } else {
            led_indicator_start(s_status_led, BSP_LED_OFF);
        }
    }
#endif
}

namespace {

/* Cached remote thermostat state used to derive heat demand. ThermostatRunningState is
 * the primary signal; LocalTemperature/OccupiedHeatingSetpoint are a fallback so that a
 * demand can still be inferred (with hysteresis) if a thermostat never reports
 * ThermostatRunningState, or reports it but the Heat bit does not change on that
 * particular device. */
chip::Optional<uint16_t> s_running_state;
chip::Optional<int16_t> s_local_temperature;
chip::Optional<int16_t> s_occupied_heating_setpoint;
bool s_temperature_based_heat_demand = false;

void app_driver_recompute_heat_demand()
{
    bool heat_from_running_state = false;
    if (s_running_state.HasValue()) {
        heat_from_running_state =
            (s_running_state.Value() & static_cast<uint16_t>(Thermostat::RelayStateBitmap::kHeat)) != 0;
    }

    if (s_local_temperature.HasValue() && s_occupied_heating_setpoint.HasValue()) {
        int16_t temperature = s_local_temperature.Value();
        int16_t setpoint = s_occupied_heating_setpoint.Value();
        int16_t hysteresis = CONFIG_THERMOSTAT_CONTROLLER_HEATING_HYSTERESIS_CENTIDEGREES;
        if (temperature <= setpoint - hysteresis) {
            s_temperature_based_heat_demand = true;
        } else if (temperature >= setpoint) {
            s_temperature_based_heat_demand = false;
        }
        /* else: within the hysteresis band, keep the previous temperature-based verdict */
    }

    bool heat_active = heat_from_running_state || s_temperature_based_heat_demand;
    ESP_LOGI(TAG, "RunningState heat=%d, LocalTemperature/OccupiedHeatingSetpoint-based heat=%d -> demand=%d",
            heat_from_running_state, s_temperature_based_heat_demand, heat_active);
    app_driver_set_heat_demand(heat_active);
}

class thermostat_state_subscribe_callback : public chip::app::ReadClient::Callback {
public:
    void OnAttributeData(const chip::app::ConcreteDataAttributePath &aPath, chip::TLV::TLVReader *aReader,
                         const chip::app::StatusIB &aStatus) override
    {
        if (aPath.mClusterId != Thermostat::Id || !aReader) {
            return;
        }

        if (aPath.mAttributeId == Thermostat::Attributes::ThermostatRunningState::Id) {
            uint16_t value;
            if (aReader->Get(value) != CHIP_NO_ERROR) {
                ESP_LOGW(TAG, "Failed to decode ThermostatRunningState");
                return;
            }
            ESP_LOGI(TAG, "ThermostatRunningState=0x%04x", value);
            s_running_state.SetValue(value);
        } else if (aPath.mAttributeId == Thermostat::Attributes::LocalTemperature::Id) {
            int16_t value;
            if (aReader->Get(value) != CHIP_NO_ERROR) {
                /* Nullable attribute: treat a decode failure (e.g. TLV null) as "unknown" */
                s_local_temperature.ClearValue();
                return;
            }
            ESP_LOGI(TAG, "LocalTemperature=%d.%02u degC", value / 100, abs(value % 100));
            s_local_temperature.SetValue(value);
        } else if (aPath.mAttributeId == Thermostat::Attributes::OccupiedHeatingSetpoint::Id) {
            int16_t value;
            if (aReader->Get(value) != CHIP_NO_ERROR) {
                ESP_LOGW(TAG, "Failed to decode OccupiedHeatingSetpoint");
                return;
            }
            ESP_LOGI(TAG, "OccupiedHeatingSetpoint=%d.%02u degC", value / 100, abs(value % 100));
            s_occupied_heating_setpoint.SetValue(value);
        } else {
            return;
        }

        app_driver_recompute_heat_demand();
    }

    void OnEventData(const chip::app::EventHeader &aEventHeader, chip::TLV::TLVReader *apData,
                     const chip::app::StatusIB *aStatus) override
    {
    }

    void OnError(CHIP_ERROR aError) override
    {
        ESP_LOGW(TAG, "Subscription error: %s", chip::ErrorStr(aError));
    }

    void OnDone(chip::app::ReadClient *apReadClient) override
    {
        ESP_LOGI(TAG, "Subscription to bound thermostat ended");
    }
};

thermostat_state_subscribe_callback s_subscribe_callback;

} // namespace

void app_driver_subscribe_thermostat_attributes(client::peer_device_t *peer_device,
                                                client::request_handle_t *req_handle, void *priv_data)
{
    uint16_t min_interval = CONFIG_THERMOSTAT_CONTROLLER_SUBSCRIBE_MIN_INTERVAL;
    uint16_t max_interval = CONFIG_THERMOSTAT_CONTROLLER_SUBSCRIBE_MAX_INTERVAL;
    bool keep_subscription = true;
    bool auto_resubscribe = true;

    uint16_t endpoint_id = req_handle->attribute_path.mEndpointId;
    chip::app::AttributePathParams paths[3] = {
        {endpoint_id, Thermostat::Id, Thermostat::Attributes::ThermostatRunningState::Id},
        {endpoint_id, Thermostat::Id, Thermostat::Attributes::LocalTemperature::Id},
        {endpoint_id, Thermostat::Id, Thermostat::Attributes::OccupiedHeatingSetpoint::Id},
    };

    client::interaction::subscribe::send_request(peer_device, paths, 3, &req_handle->event_path, 0, min_interval,
                                                 max_interval, keep_subscription, auto_resubscribe,
                                                 s_subscribe_callback);
}

void app_driver_client_callback(client::peer_device_t *peer_device, client::request_handle_t *req_handle,
                                void *priv_data)
{
    if (req_handle->type == esp_matter::client::SUBSCRIBE_ATTR) {
        app_driver_subscribe_thermostat_attributes(peer_device, req_handle, priv_data);
    }
}

void app_driver_client_group_invoke_command_callback(uint8_t fabric_index, client::request_handle_t *req_handle,
                                                      void *priv_data)
{
    /* This example never issues group commands to the bound thermostat, it only listens for its
     * running state, so there is nothing to do here. */
}

app_driver_handle_t app_driver_boiler_demand_init()
{
    /* Factory reset button */
    button_handle_t btns[BSP_BUTTON_NUM];
    ESP_ERROR_CHECK(bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM));

    /* Status LED reflecting the current heat-demand state of the bound thermostat */
#if CONFIG_BSP_LEDS_NUM > 0
    led_indicator_handle_t leds[CONFIG_BSP_LEDS_NUM];
    ESP_ERROR_CHECK(bsp_led_indicator_create(leds, NULL, CONFIG_BSP_LEDS_NUM));
    s_status_led = leds[0];
    led_indicator_start(s_status_led, BSP_LED_OFF);
#endif

    client::set_request_callback(app_driver_client_callback, app_driver_client_group_invoke_command_callback, NULL);

    return (app_driver_handle_t)btns[0];
}
