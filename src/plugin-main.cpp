#include <obs-module.h>
#include <obs-frontend-api.h>

#include "bacons-helper-dock.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("bacons-helper", "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Control Bacons Helper channel and stream settings from an OBS Studio dock.";
}

static BaconsHelperDock *dock = nullptr;

bool obs_module_load(void)
{
	dock = new BaconsHelperDock();
	if (!obs_frontend_add_dock_by_id("bacons_helper_dock", "Bacons Helper", dock)) {
		blog(LOG_ERROR, "[Bacons Helper] Unable to register the OBS dock");
		delete dock;
		dock = nullptr;
		return false;
	}

	blog(LOG_INFO, "[Bacons Helper] OBS dock loaded (version %s)", BH_OBS_VERSION);
	return true;
}

void obs_module_unload(void)
{
	// OBS owns and destroys the registered dock widget with its main window.
	dock = nullptr;
}
