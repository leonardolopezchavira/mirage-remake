// Fork addition: lets the standalone app run start-up code from inside the editor.
// The pointer is null in every plug-in format; only the standalone program
// (src/plugin/mac/StandaloneSettings.mm, linked into Mirage.app alone) sets it.
#pragma once

#include "DistrhoUtils.hpp"

START_NAMESPACE_DISTRHO

extern void (*g_mirageStandaloneStart)();

END_NAMESPACE_DISTRHO
