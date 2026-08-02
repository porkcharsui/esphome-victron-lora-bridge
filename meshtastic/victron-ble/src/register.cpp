#include "VictronTelemetryModule.h"

// Meshtastic provides a weak default for this post-setup variant hook. Building
// this source as a project object guarantees this strong definition is linked.
// Keep a concrete external symbol even under Meshtastic's whole-program LTO.
// Besides making the override auditable in release ELFs, noinline prevents the
// strong implementation from being folded into setup() with the weak default.
__attribute__((noinline, used, externally_visible)) void lateInitVariant() {
  new VictronTelemetryModule();
}
