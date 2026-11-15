// Fork addition: lets the standalone app run start-up code from inside the editor.
// The pointer is null in every plug-in format; only the standalone program
// (src/plugin/mac/StandaloneSettings.mm, linked into Mirage.app alone) sets it.
#pragma once

#include "DistrhoUtils.hpp"

#include <atomic>

namespace mirage { class MirageEngine; }

START_NAMESPACE_DISTRHO

extern void (*g_mirageStandaloneStart)();

// The engine of the plug-in instance, for the standalone's virtual MIDI port
// (src/plugin/mac/VirtualMidi.mm). Every format sets it; only the standalone reads it.
extern std::atomic<mirage::MirageEngine*> g_mirageEngine;

END_NAMESPACE_DISTRHO
