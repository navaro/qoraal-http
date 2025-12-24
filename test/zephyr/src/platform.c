/*
 *  Copyright (C) 2015-2025, Navaro, All Rights Reserved
 *  SPDX-License-Identifier: Apache-2.0
 *
 *  Licensed under the Apache License, Version 2.0 (the "License"); you may
 *  not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 *  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  This file is part of CORAL Connect (https://navaro.nl)
 */


#if defined CFG_OS_ZEPHYR

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sta, CONFIG_LOG_DEFAULT_LEVEL);

#include <zephyr/kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/printk.h>
#include <zephyr/init.h>

#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/drivers/gpio.h>

#include <stdio.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/device.h>

#include <zephyr/net/net_core.h>
#include <zephyr/net/net_pkt.h>
#include <net/wifi_ready.h>

#include <zephyr/storage/flash_map.h>
#include <pm_config.h>

#include "qoraal/qoraal.h"
#include "qoraal-http/qoraal.h"
#include "qoraal/svc/svc_services.h"
#include "qoraal/platform.h"
#include "qoraal/qshell/console.h"


#if !IS_ENABLED(CONFIG_PARTITION_MANAGER_ENABLED)
#error "PM is not enabled; you will NOT get the PM flash map."
#endif

#define WIFI_SHELL_MGMT_EVENTS              (NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT)

static void wifi_ready_cb(bool wifi_ready);
static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event, struct net_if *iface) ;
static void net_mgmt_event_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event, struct net_if *iface) ;
static void main_init (void) ;

static void wifi_connect_work_handler(struct k_work *work) ;
#define WIFI_WQ_STACK_SIZE 8192
#define WIFI_WQ_PRIORITY   5   /* Higher number = lower priority (preemptive) */

K_THREAD_STACK_DEFINE(wifi_wq_stack, WIFI_WQ_STACK_SIZE);
static struct k_work_q wifi_wq;
static struct k_work wifi_connect_work;


static struct net_mgmt_event_callback wifi_shell_mgmt_cb;
static struct net_mgmt_event_callback net_shell_mgmt_cb;


static struct {
	const struct shell *sh;
	union {
		struct {
			uint8_t connected	: 1;
			uint8_t connect_result	: 1;
			uint8_t disconnect_requested	: 1;
			uint8_t _unused		: 5;
		};
		uint8_t all;
	};
} wifi_context;

/* PM route (recommended in NCS): pm_static.yml defines a partition named "littlefs" */
#define LFS_DEV_ID  ((uintptr_t)FLASH_AREA_ID(lfs_ext))
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(lfs_cfg);
struct fs_mount_t lfs_mnt = {
	.type = FS_LITTLEFS, .mnt_point = "/lfs",
	.storage_dev = (void *)LFS_DEV_ID, 
	.fs_data = &lfs_cfg,
};

static uint32_t _platform_flash_size;

void
platform_init_wifi (void)
{

    /* Start low-priority workqueue */
    k_work_queue_start( &wifi_wq,  wifi_wq_stack, 
            K_THREAD_STACK_SIZEOF(wifi_wq_stack), WIFI_WQ_PRIORITY, NULL );

    k_work_init(&wifi_connect_work, wifi_connect_work_handler);

    // Littlefs mount
    const struct flash_area *fa;
    int rc = flash_area_open((uintptr_t)lfs_mnt.storage_dev, &fa);
    if (rc == 0) {
        flash_area_close(fa);

        rc = fs_mount(&lfs_mnt);
        if (rc == -ENODEV) { /* optional mkfs-on-first-boot */
                (void)fs_mkfs(FS_LITTLEFS, (uintptr_t)lfs_mnt.storage_dev, lfs_mnt.fs_data, 0);
                rc = fs_mount(&lfs_mnt);
        }
    }
    struct fs_statvfs st;
    rc = fs_statvfs("/lfs", &st);
    if (!rc) {
            printk("%s: bsize=%lu frsize=%lu blocks=%lu bfree=%lu (~%lu KiB total)\n",
            "/lfs", (unsigned long)st.f_bsize, (unsigned long)st.f_frsize,
            (unsigned long)st.f_blocks, (unsigned long)st.f_bfree,
            (unsigned long)(st.f_blocks * st.f_frsize / 1024));
    } else {
            printk("statvfs(%s) rc=%d\n", "/lfs", rc);
    }

    // WiFi connect
    memset(&wifi_context, 0, sizeof(wifi_context));
	net_mgmt_init_event_callback(&wifi_shell_mgmt_cb,
				     wifi_mgmt_event_handler,
				     WIFI_SHELL_MGMT_EVENTS);
	net_mgmt_add_event_callback(&wifi_shell_mgmt_cb);
	net_mgmt_init_event_callback(&net_shell_mgmt_cb,
				     net_mgmt_event_handler,
				     NET_EVENT_IPV4_DHCP_BOUND);
	net_mgmt_add_event_callback(&net_shell_mgmt_cb);

    wifi_ready_callback_t cb;
	struct net_if *iface = net_if_get_first_wifi();

	if (!iface) {
		LOG_ERR("Failed to get Wi-Fi interface");
		return -1;
	}

	cb.wifi_ready_cb = wifi_ready_cb;

	LOG_DBG("Registering Wi-Fi ready callbacks");
	rc = register_wifi_ready_callback(cb, iface);
	if (rc) {
		LOG_ERR("Failed to register Wi-Fi ready callbacks %s", strerror(rc));
		return rc;
	}


	LOG_INF("Starting %s with CPU frequency: %d MHz", CONFIG_BOARD, SystemCoreClock/MHZ(1));
	k_sleep(K_SECONDS(1));

    return ;
}

int32_t
platform_init(uint32_t flash_size)
{
    _platform_flash_size = flash_size;
    console_init();
    return 0;
}

int32_t
platform_start(void)
{
    ARG_UNUSED(_platform_flash_size);
    platform_init_wifi();
    return 0;
}

int32_t
platform_stop(void)
{
    return 0;
}

void *
platform_malloc(QORAAL_HEAP heap, size_t size)
{
    ARG_UNUSED(heap);
    return malloc(size);
}

void
platform_free(QORAAL_HEAP heap, void *mem)
{
    ARG_UNUSED(heap);
    free(mem);
}

void
platform_print(const char *format)
{
    console_write (0, format, strlen(format)) ;
}

int32_t 
platform_getch (uint32_t timeout_ms)
{
    return console_getchar(); 
}

void
platform_assert(const char *format)
{
    printk("%s", format);
    k_oops();
}

uint32_t
platform_current_time(void)
{
    return k_uptime_get_32() / 1000U;
}

uint32_t
platform_rand(void)
{
    return rand();
}

uint32_t
platform_wdt_kick(void)
{
    return 20U;
}

int32_t
platform_flash_erase(uint32_t addr_start, uint32_t addr_end)
{
    ARG_UNUSED(addr_start);
    ARG_UNUSED(addr_end);
    return E_NOIMPL;
}

int32_t
platform_flash_write(uint32_t addr, uint32_t len, const uint8_t *data)
{
    ARG_UNUSED(addr);
    ARG_UNUSED(len);
    ARG_UNUSED(data);
    return E_NOIMPL;
}

int32_t
platform_flash_read(uint32_t addr, uint32_t len, uint8_t *data)
{
    ARG_UNUSED(addr);
    ARG_UNUSED(len);
    ARG_UNUSED(data);
    return E_NOIMPL;
}


static void wifi_connect_work_handler(struct k_work *work) 
{
	struct net_if *iface = net_if_get_first_wifi();

	wifi_context.connected = false;
	wifi_context.connect_result = false;

    struct wifi_connect_req_params params = { 0 };
    params.ssid = CONFIG_WIFI_CREDENTIALS_STATIC_SSID;
    params.ssid_length = strlen(CONFIG_WIFI_CREDENTIALS_STATIC_SSID);
    params.psk = CONFIG_WIFI_CREDENTIALS_STATIC_PASSWORD;
    params.psk_length = strlen(CONFIG_WIFI_CREDENTIALS_STATIC_PASSWORD);
    params.security = WIFI_SECURITY_TYPE_PSK;
    params.channel = WIFI_CHANNEL_ANY;
    params.mfp = WIFI_MFP_OPTIONAL;

    int res = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params,
                    sizeof(params));

    LOG_INF("Connection requested: %d", res);
}

static void wifi_ready_cb(bool wifi_ready)
{
	LOG_INF("Is Wi-Fi ready?: %s", wifi_ready ? "yes" : "no");
	if (wifi_ready) {
		k_work_submit_to_queue(&wifi_wq, &wifi_connect_work);
	}

}

static int cmd_wifi_status(void)
{
	struct net_if *iface = net_if_get_default();
	struct wifi_iface_status status = { 0 };

	if (net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status,
				sizeof(struct wifi_iface_status))) {
		LOG_INF("Status request failed");

		return -ENOEXEC;
	}

	LOG_INF("==================");
	LOG_INF("State: %s", wifi_state_txt(status.state));

	if (status.state >= WIFI_STATE_ASSOCIATED) {
		uint8_t mac_string_buf[sizeof("xx:xx:xx:xx:xx:xx")];

		LOG_INF("Interface Mode: %s",
		       wifi_mode_txt(status.iface_mode));
		LOG_INF("Link Mode: %s",
		       wifi_link_mode_txt(status.link_mode));
		LOG_INF("SSID: %.32s", status.ssid);
		LOG_INF("BSSID: %s",
		       (char *)net_sprint_ll_addr_buf(
				status.bssid, WIFI_MAC_ADDR_LEN,
				mac_string_buf, sizeof(mac_string_buf)));
		LOG_INF("Band: %s", wifi_band_txt(status.band));
		LOG_INF("Channel: %d", status.channel);
		LOG_INF("Security: %s", wifi_security_txt(status.security));
		LOG_INF("MFP: %s", wifi_mfp_txt(status.mfp));
		LOG_INF("RSSI: %d", status.rssi);
	}
	return 0;
}

static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
				     uint32_t mgmt_event, struct net_if *iface)
{
	switch (mgmt_event) {
	case NET_EVENT_WIFI_CONNECT_RESULT: {
        const struct wifi_status *status =
            (const struct wifi_status *) cb->info;

        if (wifi_context.connected) {
            break;
        }
        if (status->status) {
            LOG_ERR("Connection failed (%d)", status->status);
        } else {
            LOG_INF("Connected");
            wifi_context.connected = true;
        }
        wifi_context.connect_result = true;       
        
        cmd_wifi_status();    
    }
	break;
	case NET_EVENT_WIFI_DISCONNECT_RESULT: {
        const struct wifi_status *status =
            (const struct wifi_status *) cb->info;

        if (!wifi_context.connected) {
            return;
        }

        if (wifi_context.disconnect_requested) {
            LOG_INF("Disconnection request %s (%d)",
                status->status ? "failed" : "done",
                        status->status);
            wifi_context.disconnect_requested = false;
        } else {
            LOG_INF("Received Disconnected");
            wifi_context.connected = false;
        }

        cmd_wifi_status();        
    }
	break;
	default:
		break;
	}
}


static void net_mgmt_event_handler(struct net_mgmt_event_callback *cb,
				    uint32_t mgmt_event, struct net_if *iface)
{
	switch (mgmt_event) {
	case NET_EVENT_IPV4_DHCP_BOUND: {
        /* Get DHCP info from struct net_if_dhcpv4 and print */
        const struct net_if_dhcpv4 *dhcpv4 = cb->info;
        const struct in_addr *addr = &dhcpv4->requested_ip;
        char dhcp_info[128];

        net_addr_ntop(AF_INET, addr, dhcp_info, sizeof(dhcp_info));

        LOG_INF("DHCP IP address: %s", dhcp_info);

    }
	break;
	default:
	break;
	}
}


#endif /* CFG_OS_ZEPHYR */
