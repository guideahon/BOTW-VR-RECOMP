// The VR mode's options (runtime/vr/vr_options.c): settings.ini / the Options screen's VR page, or a SAVAGEWIND_VR_*
// variable. VR_OPT_* ids and their meanings: runtime/settings.h VR_OPTIONS.
#pragma once
#include <stdbool.h>

#include "../settings.h"

int vr_opt(int id);            // the option's number now (0: its default)
bool vr_opt_on(int id);        // a toggle on by default (VR_OPT_RUMBLE, _MOTION, ...): on now
