// Mirage plug-in identity and DPF configuration.
#pragma once

#define DISTRHO_PLUGIN_BRAND "mirage-remake"
#define DISTRHO_PLUGIN_NAME "Mirage"
#define DISTRHO_PLUGIN_URI "urn:mirage-remake:mirage"
#define DISTRHO_PLUGIN_CLAP_ID "mirage-remake.mirage"
#define DISTRHO_PLUGIN_BRAND_ID MRmk
#define DISTRHO_PLUGIN_UNIQUE_ID Mrge

#define DISTRHO_PLUGIN_HAS_UI 1
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1
#define DISTRHO_UI_USE_NANOVG 1
#define DISTRHO_UI_FILE_BROWSER 1
#define DISTRHO_UI_DEFAULT_WIDTH 1200
#define DISTRHO_UI_DEFAULT_HEIGHT 410
#define DISTRHO_UI_USER_RESIZABLE 1
#define DISTRHO_PLUGIN_IS_RT_SAFE 1
#define DISTRHO_PLUGIN_IS_SYNTH 1
#define DISTRHO_PLUGIN_NUM_INPUTS 2   // audio in for sampling (mixed to mono)
#define DISTRHO_PLUGIN_NUM_OUTPUTS 2
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT 1
#define DISTRHO_PLUGIN_WANT_MIDI_OUTPUT 0
#define DISTRHO_PLUGIN_WANT_STATE 1
#define DISTRHO_PLUGIN_WANT_FULL_STATE 1

#define DISTRHO_PLUGIN_VST3_CATEGORIES "Instrument|Sampler"
#define DISTRHO_PLUGIN_CLAP_FEATURES "instrument", "sampler", "stereo"
#define DISTRHO_PLUGIN_AU_TYPE aumu
