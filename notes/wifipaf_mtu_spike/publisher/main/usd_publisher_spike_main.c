/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * WiFiPAF MTU/timing spike -- "publisher" side (stand-in for a Matter
 * commissionee). Adapted from ESP-IDF's stock usd_publisher example: instead
 * of sending one follow-up reply, it fires a back-to-back burst of
 * PAF_SPIKE_FRAG_COUNT fragments of PAF_SPIKE_FRAG_SIZE bytes each, once
 * triggered by the subscriber -- to see whether a PAFTP-sized multi-fragment
 * transfer survives real NAN-USD follow-up delivery. See ../README.md.
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_nan.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#define MAC_ADDR_LEN 6

#define EXAMPLE_USD_SVC_NAME CONFIG_ESP_WIFI_USD_PUB_SVC_NAME
#define EXAMPLE_USD_TTL      CONFIG_ESP_WIFI_USD_PUB_TTL
#define FRAG_SIZE            CONFIG_PAF_SPIKE_FRAG_SIZE
#define FRAG_COUNT           CONFIG_PAF_SPIKE_FRAG_COUNT

static EventGroupHandle_t s_nan_event_group;
static const int NAN_TRIGGER = BIT0;
static const char *TAG = "usd_publisher_spike";

static uint8_t g_peer_inst_id;
static uint8_t g_peer_mac[MAC_ADDR_LEN];
static uint8_t g_publish_id;

static void nan_receive_event_handler(void *arg, esp_event_base_t event_base,
                                       int32_t event_id, void *event_data)
{
    wifi_event_nan_receive_t *evt = (wifi_event_nan_receive_t *)event_data;
    if (evt == NULL) {
        return;
    }
    /* Only care about the subscriber's initial "start" trigger here. */
    g_peer_inst_id = evt->peer_inst_id;
    memcpy(g_peer_mac, evt->peer_if_mac, MAC_ADDR_LEN);
    ESP_LOGI(TAG, "Trigger received from peer id %d, mac " MACSTR, evt->peer_inst_id,
             MAC2STR(evt->peer_if_mac));
    xEventGroupSetBits(s_nan_event_group, NAN_TRIGGER);
}

/* Fills a FRAG_SIZE buffer with: [seq:2B big-endian][0x00 filler]. */
static void build_fragment(uint8_t *buf, uint16_t seq)
{
    buf[0] = (uint8_t)(seq >> 8);
    buf[1] = (uint8_t)(seq & 0xFF);
    if (FRAG_SIZE > 2) {
        memset(buf + 2, (int)(seq & 0xFF), FRAG_SIZE - 2);
    }
}

static void send_burst(void)
{
    uint8_t frag[FRAG_SIZE];
    int64_t burst_start_us = esp_timer_get_time();
    int failures = 0;

    ESP_LOGI(TAG, "Sending burst: %d fragments x %d bytes to peer id %d", FRAG_COUNT, FRAG_SIZE,
             g_peer_inst_id);

    for (uint16_t seq = 0; seq < FRAG_COUNT; seq++) {
        build_fragment(frag, seq);

        wifi_nan_followup_params_t fup_params = {
            .inst_id      = g_publish_id,
            .peer_inst_id = g_peer_inst_id,
            .ssi          = frag,
            .ssi_len      = FRAG_SIZE,
        };
        memcpy(fup_params.peer_mac, g_peer_mac, MAC_ADDR_LEN);

        int64_t t0 = esp_timer_get_time();
        esp_err_t err = esp_wifi_nan_send_message(&fup_params);
        int64_t t1 = esp_timer_get_time();

        if (err != ESP_OK) {
            failures++;
            ESP_LOGE(TAG, "  frag %2u: esp_wifi_nan_send_message failed: %s (call took %lld us)", seq,
                      esp_err_to_name(err), (long long)(t1 - t0));
        } else {
            ESP_LOGI(TAG, "  frag %2u: queued ok (call took %lld us)", seq, (long long)(t1 - t0));
        }
    }

    int64_t burst_end_us = esp_timer_get_time();
    ESP_LOGI(TAG, "--- publisher burst summary ---");
    ESP_LOGI(TAG, "queued %d/%d fragments (%d send failures)", FRAG_COUNT - failures, FRAG_COUNT, failures);
    ESP_LOGI(TAG, "wall time to queue whole burst: %lld ms", (long long)((burst_end_us - burst_start_us) / 1000));
    ESP_LOGI(TAG, "(note: this measures local queuing time only -- check the SUBSCRIBER's log for");
    ESP_LOGI(TAG, " actual over-the-air arrival count/timing/gaps, which is what actually matters)");
}

void wifi_usd_publish(void)
{
    s_nan_event_group = xEventGroupCreate();
    esp_event_handler_instance_t instance_any_id;

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_NAN_RECEIVE,
                                                          &nan_receive_event_handler, NULL,
                                                          &instance_any_id));

    ESP_RETURN_VOID_ON_ERROR(esp_wifi_start(), TAG, "NAN-USD failed to start Wi-Fi");
    ESP_RETURN_VOID_ON_ERROR(esp_wifi_nan_usd_start(), TAG, "NAN-USD initialization failed");

    wifi_nan_publish_cfg_t publish_cfg = {
        .service_name       = EXAMPLE_USD_SVC_NAME,
        .ttl                = EXAMPLE_USD_TTL,
        .ssi                = NULL,
        .ssi_len            = 0,
        .usd_discovery_flag = 1,
        .usd_publish_config = esp_wifi_usd_get_default_publish_cfg(),
    };

    g_publish_id = esp_wifi_nan_publish_service(&publish_cfg);
    if (g_publish_id == 0) {
        ESP_LOGE(TAG, "Publishing to %s failed", publish_cfg.service_name);
        return;
    }
    ESP_LOGI(TAG, "Publishing '%s', waiting for subscriber trigger (TTL %d s)...", EXAMPLE_USD_SVC_NAME,
             EXAMPLE_USD_TTL);

    EventBits_t bits = xEventGroupWaitBits(s_nan_event_group, NAN_TRIGGER, pdFALSE, pdFALSE,
                                            pdMS_TO_TICKS(EXAMPLE_USD_TTL * 1000));
    if (bits & NAN_TRIGGER) {
        send_burst();
    } else {
        ESP_LOGW(TAG, "Timed out waiting for a subscriber trigger");
    }

    esp_wifi_nan_cancel_service(g_publish_id);
    esp_wifi_nan_usd_stop();
}

void initialise_wifi(void)
{
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    initialise_wifi();
    wifi_usd_publish();
}
