/*
AI Assistant for OBS Studio
Copyright (C) 2026 AllegroMoltoV

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "dock.h"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>
#include <util/config-file.h>

#include <QMainWindow>
#include <QPointer>

#include <curl/curl.h>

#include <string>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {

constexpr const char *kDockId = "ai-assistant-for-obs-dock";

QPointer<aia::Dock> g_dock;

// 実行時に読み込まれた libcurl の版と HTTPS の対応をログに残す
void log_curl_runtime()
{
	const curl_version_info_data *info = curl_version_info(CURLVERSION_NOW);
	if (!info) {
		obs_log(LOG_WARNING, "curl_version_info returned null");
		return;
	}
	bool has_https = false;
	for (const char *const *p = info->protocols; p && *p; ++p)
		has_https = has_https || std::string(*p) == "https";
	obs_log(LOG_INFO, "libcurl runtime version=%s ssl=%s https=%d", info->version,
		info->ssl_version ? info->ssl_version : "none", has_https ? 1 : 0);
}

void register_dock()
{
	auto *main_window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!main_window)
		return;

	auto *dock = new aia::Dock();
	dock->setObjectName(kDockId);
	if (!obs_frontend_add_dock_by_id(kDockId, obs_module_text("Dock.Title"), dock)) {
		obs_log(LOG_ERROR, "failed to add dock");
		delete dock;
		return;
	}
	g_dock = dock;

	// OBS は保存済みのドックの配置をこの通知の前に復元し、新しく足したドックを隠すため、配置を再適用する
	config_t *user_config = obs_frontend_get_user_config();
	const char *saved_state = user_config ? config_get_string(user_config, "BasicWindow", "DockState") : nullptr;
	if (saved_state && *saved_state)
		main_window->restoreState(QByteArray::fromBase64(QByteArray(saved_state)));
	obs_log(LOG_INFO, "dock registered");
}

void on_frontend_event(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING)
		register_dock();
}

} // namespace

bool obs_module_load(void)
{
	curl_global_init(CURL_GLOBAL_DEFAULT);
	log_curl_runtime();
	obs_frontend_add_event_callback(on_frontend_event, nullptr);
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(on_frontend_event, nullptr);
	// ドックは OBS が破棄する
	g_dock = nullptr;
	curl_global_cleanup();
	obs_log(LOG_INFO, "plugin unloaded");
}
