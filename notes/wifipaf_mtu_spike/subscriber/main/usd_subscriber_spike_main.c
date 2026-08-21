/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 *
 * WiFiPAF MTU/timing spike -- "subscriber" side (stand-in for a Matter
 * commissioner). Adapted from ESP-IDF's stock usd_subscriber example: on
 * service match it sends a tiny trigger follow-up, then times and validates
 * the burst of fragments the publisher sends back. See ../README.md.
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
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#define MAC_ADDR_LEN 6

#define EXAMPLE_USD_SVC_NAME            CONFIG_ESP_WIFI_USD_SUB_SVC_NAME
#define EXAMPLE_USD_TTL                 CONFIG_ESP_WIFI_USD_SUB_TTL
#define EXAMPLE_USD_DEFAULT_SUB_CHANNEL CONFIG_ESP_WIFI_USD_DEFAULT_CHAN
#define FRAG_COUNT                      CONFIG_PAF_SPIKE_FRAG_COUNT
#define FRAG_TIMEOUT_MS                 CONFIG_PAF_SPIKE_FRAG_TIMEOUT_MS

static EventGroupHandle_t s_nan_event_group;
static const int NAN_SRV_MATCH = BIT0;
static const int NAN_RECEIVE   = BIT1;
static const char *TAG = "usd_subscriber_spike";

static uint8_t g_peer_inst_id;
static uint8_t g_peer_mac[MAC_ADDR_LEN];
static uint8_t g_subscribe_id;

static bool g_seen[FRAG_COUNT];
static int64_t g_arrival_us[FRAG_COUNT];
static int g_received_count;
static int64_t g_trigger_sent_us;

static void nan_svc_match_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                                         void *event_data)
{
    wifi_event_nan_svc_match_t *evt = (wifi_event_nan_svc_match_t *)event_data;
    if (evt == NULL) {
        return;
    }
    g_peer_inst_id = evt->publish_id;
    memcpy(g_peer_mac, evt->pub_if_mac, MAC_ADDR_LEN);
    ESP_LOGI(TAG, "Service matched with peer_id %d peer mac " MACSTR, evt->publish_id,
             MAC2STR(evt->pub_if_mac));
    xEventGroupSetBits(s_nan_event_group, NAN_SRV_MATCH);
}

static void nan_receive_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                                       void *event_data)
{
    wifi_event_nan_receive_t *evt = (wifi_event_nan_receive_t *)event_data;
    if (evt == NULL || evt->ssi_len < 2) {
        return;
    }
    int64_t now = esp_timer_get_time();
    uint16_t seq = ((uint16_t)evt->ssi[0] << 8) | evt->ssi[1];

    if (seq < FRAG_COUNT && !g_seen[seq]) {
        g_seen[seq]       = true;
        g_arrival_us[seq] = now;
        g_received_count++;
        ESP_LOGI(TAG, "  frag %2u arrived (%d bytes, +%lld ms since trigger)", seq, evt->ssi_len,
                 (long long)((now - g_trigger_sent_us) / 1000));
    } else if (seq < FRAG_COUNT) {
        ESP_LOGW(TAG, "  frag %2u arrived AGAIN (duplicate delivery)", seq);
    } else {
        ESP_LOGW(TAG, "  frag %2u arrived but is out of the expected 0..%d range", seq, FRAG_COUNT - 1);
    }
    xEventGroupSetBits(s_nan_event_group, NAN_RECEIVE);
}

static void print_summary(void)
{
    ESP_LOGI(TAG, "--- burst summary ---");
    ESP_LOGI(TAG, "received %d/%d fragments", g_received_count, FRAG_COUNT);

    if (g_received_count < FRAG_COUNT) {
        ESP_LOGW(TAG, "gaps: missing seq(s):");
        for (int i = 0; i < FRAG_COUNT; i++) {
            if (!g_seen[i]) {
                ESP_LOGW(TAG, "  - %d", i);
            }
        }
    } else {
        ESP_LOGI(TAG, "gaps: none");
    }

    if (g_received_count == 0) {
        return;
    }

    int64_t first_us = -1, last_us = -1;
    int64_t max_gap_us = 0, sum_gap_us = 0;
    int gap_samples = 0;
    int64_t prev_us = -1;

    for (int i = 0; i < FRAG_COUNT; i++) {
        if (!g_seen[i]) {
            continue;
        }
        if (first_us < 0) {
            first_us = g_arrival_us[i];
        }
        last_us = g_arrival_us[i];
        if (prev_us >= 0) {
            int64_t gap = g_arrival_us[i] - prev_us;
            sum_gap_us += gap;
            gap_samples++;
            if (gap > max_gap_us) {
                max_gap_us = gap;
            }
        }
        prev_us = g_arrival_us[i];
    }

    ESP_LOGI(TAG, "trigger -> first fragment: %lld ms", (long long)((first_us - g_trigger_sent_us) / 1000));
    ESP_LOGI(TAG, "first -> last fragment (burst span): %lld ms", (long long)((last_us - first_us) / 1000));
    if (gap_samples > 0) {
        ESP_LOGI(TAG, "avg inter-fragment gap: %lld ms", (long long)((sum_gap_us / gap_samples) / 1000));
        ESP_LOGI(TAG, "max inter-fragment gap: %lld ms", (long long)(max_gap_us / 1000));
    }
}

void wifi_usd_subscribe(void)
{
    s_nan_event_group = xEventGroupCreate();
    esp_event_handler_instance_t instance_any_id;

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_NAN_SVC_MATCH,
                                                          &nan_svc_match_event_handler, NULL,
                                                          &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_NAN_RECEIVE,
                                                          &nan_receive_event_handler, NULL,
                                                          &instance_any_id));

    ESP_RETURN_VOID_ON_ERROR(esp_wifi_start(), TAG, "NAN-USD failed to start Wi-Fi");
    ESP_RETURN_VOID_ON_ERROR(esp_wifi_nan_usd_start(), TAG, "NAN-USD initialization failed");

    wifi_nan_subscribe_cfg_t subscribe_cfg = {
        .service_name         = EXAMPLE_USD_SVC_NAME,
        .ttl                  = EXAMPLE_USD_TTL,
        .ssi                  = NULL,
        .ssi_len              = 0,
        .usd_discovery_flag   = 1,
        .usd_subscribe_config = esp_wifi_usd_get_default_subscribe_cfg(),
    };
    subscribe_cfg.usd_subscribe_config.usd_default_channel = EXAMPLE_USD_DEFAULT_SUB_CHANNEL;

    g_subscribe_id = esp_wifi_nan_subscribe_service(&subscribe_cfg);
    if (g_subscribe_id == 0) {
        ESP_LOGE(TAG, "Subscribing to %s failed", subscribe_cfg.service_name);
        return;
    }
    ESP_LOGI(TAG, "Subscribed to '%s', waiting for a publisher (TTL %d s)...", EXAMPLE_USD_SVC_NAME,
             EXAMPLE_USD_TTL);

    EventBits_t bits = xEventGroupWaitBits(s_nan_event_group, NAN_SRV_MATCH, pdTRUE, pdFALSE,
                                            pdMS_TO_TICKS(EXAMPLE_USD_TTL * 1000));
    if (!(bits & NAN_SRV_MATCH)) {
        ESP_LOGW(TAG, "Timed out waiting for a service match");
        return;
    }

    /* Trigger the publisher's burst with a minimal follow-up. */
    const char *go = "GO";
    wifi_nan_followup_params_t trig = {
        .inst_id      = g_subscribe_id,
        .peer_inst_id = g_peer_inst_id,
        .ssi          = (uint8_t *)go,
        .ssi_len      = strlen(go),
    };
    memcpy(trig.peer_mac, g_peer_mac, MAC_ADDR_LEN);
    g_trigger_sent_us = esp_timer_get_time();
    ESP_RETURN_VOID_ON_ERROR(esp_wifi_nan_send_message(&trig), TAG, "Sending trigger failed!");
    ESP_LOGI(TAG, "Trigger sent, waiting for %d fragment(s) (timeout %d ms)...", FRAG_COUNT,
             FRAG_TIMEOUT_MS);

    /* Wait until either all fragments arrived or the deadline passes. */
    int64_t deadline_us = g_trigger_sent_us + (int64_t)FRAG_TIMEOUT_MS * 1000;
    while (g_received_count < FRAG_COUNT) {
        int64_t remaining_us = deadline_us - esp_timer_get_time();
        if (remaining_us <= 0) {
            break;
        }
        xEventGroupWaitBits(s_nan_event_group, NAN_RECEIVE, pdTRUE, pdFALSE,
                             pdMS_TO_TICKS(remaining_us / 1000 + 1));
    }

    print_summary();
    esp_wifi_nan_cancel_service(g_subscribe_id);
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
    wifi_usd_subscribe();
}
