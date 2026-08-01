/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <esp_err.h>
#include <esp_matter.h>
#include <esp_matter_client.h>

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include "esp_openthread_types.h"
#endif

typedef void *app_driver_handle_t;

/** Initialize the boiler demand driver
 *
 * This initializes the button (factory reset) and the status LED used to indicate
 * whether the bound thermostat is currently calling for heat.
 *
 * @return Handle on success.
 * @return NULL in case of failure.
 */
app_driver_handle_t app_driver_boiler_demand_init();

/** Subscribe to the ThermostatRunningState, LocalTemperature and OccupiedHeatingSetpoint
 * attributes of a newly bound thermostat.
 *
 * Called from the client request callback once a CASE session has been established with the
 * bound peer device (see app_driver_client_callback()).
 */
void app_driver_subscribe_thermostat_attributes(esp_matter::client::peer_device_t *peer_device,
                                                esp_matter::client::request_handle_t *req_handle, void *priv_data);

/** Client request send callback, registered with esp_matter::client::set_request_callback() */
void app_driver_client_callback(esp_matter::client::peer_device_t *peer_device,
                                esp_matter::client::request_handle_t *req_handle, void *priv_data);

/** Group client request send callback, registered with esp_matter::client::set_request_callback() */
void app_driver_client_group_invoke_command_callback(uint8_t fabric_index,
                                                      esp_matter::client::request_handle_t *req_handle,
                                                      void *priv_data);

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#define ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG()                                           \
    {                                                                                   \
        .radio_mode = RADIO_MODE_NATIVE,                                                \
    }

#define ESP_OPENTHREAD_DEFAULT_HOST_CONFIG()                                            \
    {                                                                                   \
        .host_connection_mode = HOST_CONNECTION_MODE_NONE,                              \
    }

#define ESP_OPENTHREAD_DEFAULT_PORT_CONFIG()                                            \
    {                                                                                   \
        .storage_partition_name = "nvs", .netif_queue_size = 10, .task_queue_size = 10, \
    }
#endif
