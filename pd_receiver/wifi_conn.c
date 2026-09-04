#include "wifi_conn.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "cmsis_os2.h"
#include "lwip/netifapi.h"
#include "soc_osal.h"
#include "wifi_hotspot.h"
#include "wifi_hotspot_config.h"

#define PD_WIFI_NETIF_NAME "wlan0"
#define WIFI_SCAN_AP_LIMIT 64U
#define WIFI_RETRY_TICKS 100U
#define WIFI_DHCP_TICKS 50U

typedef enum {
    PORT_WIFI_IDLE = 0,
    PORT_WIFI_SCANNING,
    PORT_WIFI_SCAN_DONE,
    PORT_WIFI_CONNECTING,
    PORT_WIFI_CONNECTED,
} port_wifi_state_t;

static volatile port_wifi_state_t g_state = PORT_WIFI_IDLE;
static wifi_sta_config_stru g_target;
static bool g_target_valid;

static void scan_changed(int32_t state, int32_t size)
{
    (void)state;
    (void)size;
    if (g_state == PORT_WIFI_SCANNING) {
        g_state = PORT_WIFI_SCAN_DONE;
    }
}

static void connection_changed(int32_t state, const wifi_linked_info_stru *info, int32_t reason)
{
    (void)info;
    (void)reason;
    if (state == WIFI_STATE_AVALIABLE) {
        g_state = PORT_WIFI_CONNECTED;
        return;
    }

    g_state = PORT_WIFI_IDLE;
    /* Preserve the ESP32 port's automatic reconnect behavior. */
    if (g_target_valid) {
        g_state = PORT_WIFI_CONNECTING;
        if (wifi_sta_connect(&g_target) != ERRCODE_SUCC) {
            g_state = PORT_WIFI_IDLE;
        }
    }
}

static const wifi_event_stru g_events = {
    .wifi_event_connection_changed = connection_changed,
    .wifi_event_scan_state_changed = scan_changed,
};

static int find_target(wifi_sta_config_stru *target)
{
    uint32_t count = WIFI_SCAN_AP_LIMIT;
    const size_t ssid_len = strlen(WIFI_SSID);
    const size_t key_len = strlen(WIFI_PASS);
    const size_t bytes = sizeof(wifi_scan_info_stru) * WIFI_SCAN_AP_LIMIT;
    wifi_scan_info_stru *list;

    if ((ssid_len == 0U) || (ssid_len >= sizeof(target->ssid)) ||
        (key_len >= sizeof(target->pre_shared_key))) {
        osal_printk("[PD_WIFI] invalid SSID/password length\r\n");
        return -1;
    }

    list = osal_kmalloc(bytes, OSAL_GFP_ATOMIC);
    if (list == NULL) {
        return -1;
    }
    (void)memset(list, 0, bytes);
    if (wifi_sta_get_scan_info(list, &count) != ERRCODE_SUCC) {
        osal_kfree(list);
        return -1;
    }

    for (uint32_t i = 0; i < count; ++i) {
        if ((strlen((const char *)list[i].ssid) != ssid_len) ||
            (memcmp(list[i].ssid, WIFI_SSID, ssid_len) != 0)) {
            continue;
        }
        (void)memset(target, 0, sizeof(*target));
        (void)memcpy(target->ssid, WIFI_SSID, ssid_len);
        (void)memcpy(target->bssid, list[i].bssid, sizeof(target->bssid));
        (void)memcpy(target->pre_shared_key, WIFI_PASS, key_len);
        target->security_type = list[i].security_type;
        target->ip_type = DHCP;
        osal_kfree(list);
        return 0;
    }

    osal_kfree(list);
    return -1;
}

void wifi_init_sta(void)
{
    struct netif *netif = NULL;

    while (wifi_register_event_cb(&g_events) != ERRCODE_SUCC) {
        (void)osDelay(WIFI_RETRY_TICKS);
    }
    while (wifi_is_wifi_inited() == 0) {
        (void)osDelay(WIFI_RETRY_TICKS);
    }
    while (wifi_sta_enable() != ERRCODE_SUCC) {
        (void)osDelay(WIFI_RETRY_TICKS);
    }

    while (g_state != PORT_WIFI_CONNECTED) {
        g_state = PORT_WIFI_SCANNING;
        if (wifi_sta_scan() != ERRCODE_SUCC) {
            g_state = PORT_WIFI_IDLE;
            (void)osDelay(WIFI_RETRY_TICKS);
            continue;
        }
        while (g_state == PORT_WIFI_SCANNING) {
            (void)osDelay(1U);
        }
        if ((g_state != PORT_WIFI_SCAN_DONE) || (find_target(&g_target) != 0)) {
            g_state = PORT_WIFI_IDLE;
            (void)osDelay(WIFI_RETRY_TICKS);
            continue;
        }
        g_target_valid = true;
        g_state = PORT_WIFI_CONNECTING;
        if (wifi_sta_connect(&g_target) != ERRCODE_SUCC) {
            g_state = PORT_WIFI_IDLE;
            continue;
        }
        while (g_state == PORT_WIFI_CONNECTING) {
            (void)osDelay(1U);
        }
    }

    while (netif == NULL) {
        netif = netifapi_netif_find(PD_WIFI_NETIF_NAME);
        if (netif == NULL) {
            (void)osDelay(WIFI_RETRY_TICKS);
        }
    }
    while (netifapi_dhcp_start(netif) != ERR_OK) {
        (void)osDelay(WIFI_RETRY_TICKS);
    }
    while (netifapi_dhcp_is_bound(netif) != ERR_OK) {
        (void)osDelay(WIFI_DHCP_TICKS);
    }
    osal_printk("[PD_WIFI] connected and DHCP bound\r\n");
}
