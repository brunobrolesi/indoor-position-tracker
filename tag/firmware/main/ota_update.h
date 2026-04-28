#pragma once

/* Returns immediately if OTA_TRIGGER_GPIO is not asserted LOW.
 * Does not return if OTA runs (device reboots on success or failure). */
void ota_check_and_run(void);
