/* SPDX-License-Identifier: Apache-2.0
 *
 * Entry point called from main.c — delegates to the demo manager.
 */

#include "ui.h"
#include "demo_manager.h"

void ui_start(void)
{
	demo_manager_start();
}
