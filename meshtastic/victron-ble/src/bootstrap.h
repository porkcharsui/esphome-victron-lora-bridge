#pragma once

// PlatformIO injects this header into Meshtastic's Modules.cpp translation
// unit. The implementation therefore receives the firmware project's final
// compiler flags and dependency include paths without living in its source
// tree. register.cpp supplies the strong lateInitVariant() implementation.
#include "VictronDecoder.cpp"
#include "VictronTelemetryModule.cpp"
#include "register.cpp"
