#include "config.h"

const char* const EXT_SECTION = "MaxPane_cpp";

const TabColor TAB_COLORS[] = {
  {"Default",  0,   0,   0  },  // 0: no color
  {"Red",      180, 60,  60 },  // 1
  {"Orange",   190, 120, 50 },  // 2
  {"Yellow",   180, 170, 50 },  // 3
  {"Green",    60,  150, 70 },  // 4
  {"Blue",     60,  100, 180},  // 5
  {"Purple",   130, 70,  170},  // 6
  {"Pink",     170, 70,  130},  // 7
  {"Cyan",     50,  150, 160},  // 8
};

const char* const PRESET_NAMES[] = {
    "Left + Right Split",
    "Two Columns",
    "Three Columns",
    "2x2 Grid",
    "Top + Bottom Split"
};

const int PRESET_PANE_COUNT[] = { 3, 2, 3, 4, 3 };

const WindowDef KNOWN_WINDOWS[] = {
  // Mixing & Routing
  {"Mixer",                 "Mixer",                 nullptr,          40078, 0},
  {"Track Manager",         "Track Manager",         nullptr,          40906, 2},
  {"Routing Matrix",        "Routing Matrix",        nullptr,          40251, 1},
  // Browsing & Media
  {"Media Explorer",        "Media Explorer",        nullptr,          50124, 0},
  {"FX Browser",            "Browse FX",             "Add FX",         40271, 1},
  {"Project Bay",           "Project Bay",           nullptr,          41157, 1},
  // Regions
  {"Region Manager",        "Region/Marker Manager", "Region",         40326, 1},
  {"Region Render Matrix",  "Region Render Matrix",  nullptr,          41888, 1},
  // Editing
  {"Actions",               "Actions",               nullptr,          40605, 2},
  {"Undo History",          "Undo History",           nullptr,          40072, 2},
  // Monitoring
  {"Navigator",             "Navigator",             nullptr,          40268, 2},
  {"Big Clock",             "Big Clock",             nullptr,          40378, 2},
  {"Video",                 "Video Window",          "Video",          50125, 0},
  {"Performance Meter",     "Performance Meter",     nullptr,          40240, 2},
  // Instruments
  {"Virtual MIDI Keyboard", "Virtual MIDI Keyboard", "MIDI Keyboard",  40377, 2},
};

const int NUM_KNOWN_WINDOWS = sizeof(KNOWN_WINDOWS) / sizeof(KNOWN_WINDOWS[0]);
