#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND   "Crate Audio"
#define DISTRHO_PLUGIN_NAME    "CrateChop"
#define DISTRHO_PLUGIN_URI     "urn:crateaudio:cratechop"
#define DISTRHO_PLUGIN_CLAP_ID "audio.crate.cratechop"

#define DISTRHO_PLUGIN_BRAND_ID  CrAu
#define DISTRHO_PLUGIN_UNIQUE_ID CrCh

#define DISTRHO_PLUGIN_HAS_UI              1
#define DISTRHO_PLUGIN_IS_RT_SAFE          1
#define DISTRHO_PLUGIN_IS_SYNTH            1
#define DISTRHO_PLUGIN_NUM_INPUTS          0
#define DISTRHO_PLUGIN_NUM_OUTPUTS         2
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT     1
#define DISTRHO_PLUGIN_WANT_STATE          1
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS  1
#define DISTRHO_UI_USE_NANOVG              1
#define DISTRHO_UI_FILE_BROWSER            1
#define DISTRHO_UI_USER_RESIZABLE          0
#define DISTRHO_UI_DEFAULT_WIDTH           1040
#define DISTRHO_UI_DEFAULT_HEIGHT          720

#define DISTRHO_PLUGIN_VST3_CATEGORIES "Instrument|Sampler"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "instrument", "sampler", "stereo"

#endif
