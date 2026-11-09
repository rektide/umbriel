#include "check.h"
#include "output/power_overrides.h"

using umbriel::OutputIdentity;
using umbriel::OutputPowerOverrides;

namespace {

  OutputIdentity panel(std::string_view connector, std::string_view serial) {
    return {.connector = connector, .make = "Example", .model = "Panel", .serial = serial};
  }

} // namespace

UMBRIEL_TEST(freshSessionIsLitAndOnForgetsOff) {
  OutputPowerOverrides overrides;
  const OutputIdentity output = panel("DP-1", "A");
  CHECK(!overrides.isOff(output));
  overrides.remember(output, true);
  CHECK(overrides.isOff(output));
  overrides.remember(output, true);
  overrides.remember(output, false);
  CHECK(!overrides.isOff(output));
}

UMBRIEL_TEST(sameConnectorRequiresTheSameDescriptor) {
  OutputPowerOverrides overrides;
  overrides.remember(panel("DP-1", "A"), true);
  CHECK(overrides.isOff(panel("DP-1", "A")));
  CHECK(!overrides.isOff(panel("DP-1", "B")));
  CHECK(!overrides.isOff({.connector = "DP-1", .make = "Other", .model = "Panel", .serial = "A"}));
  CHECK(!overrides.isOff({.connector = "DP-1", .make = "Example", .model = "Other", .serial = "A"}));
}

UMBRIEL_TEST(identicalUnobservedPeerDoesNotInheritOff) {
  OutputPowerOverrides overrides;
  overrides.remember(panel("DP-1", "same"), true);
  CHECK(!overrides.isOff(panel("DP-2", "same")));
  CHECK(overrides.isOff(panel("DP-1", "same")));
  overrides.remember(panel("DP-2", "same"), true);
  overrides.remember(panel("DP-1", "same"), false);
  CHECK(overrides.isOff(panel("DP-2", "same")));
  CHECK(!overrides.isOff(panel("DP-3", "same")));
}

UMBRIEL_TEST(missingDescriptorStaysConnectorBound) {
  OutputPowerOverrides overrides;
  const OutputIdentity output{.connector = "HEADLESS-1", .make = {}, .model = {}, .serial = {}};
  overrides.remember(output, true);
  CHECK(overrides.isOff(output));
  CHECK(!overrides.isOff({.connector = "HEADLESS-2", .make = {}, .model = {}, .serial = {}}));
  CHECK(!overrides.isOff(panel("HEADLESS-1", "A")));
  // A reported literal "Unknown" is not missing identity.
  CHECK(!overrides.isOff({.connector = "HEADLESS-1", .make = "Unknown", .model = "Unknown", .serial = "Unknown"}));
  overrides.clear();
  overrides.remember(panel("HEADLESS-1", "A"), true);
  CHECK(!overrides.isOff(output));
}

UMBRIEL_TEST(noConnectorFailsLit) {
  OutputPowerOverrides overrides;
  overrides.remember(panel({}, "A"), true);
  overrides.remember({}, true);
  CHECK(!overrides.isOff(panel({}, "A")));
  CHECK(!overrides.isOff({}));
}

UMBRIEL_TEST(identityStringsAreOwnedAndMatchingIsCaseInsensitive) {
  OutputPowerOverrides overrides;
  {
    const std::string connector = "DP-1";
    const std::string serial = "SERIAL";
    overrides.remember(panel(connector, serial), true);
  }
  CHECK(overrides.isOff(panel("dp-1", "serial")));
  overrides.remember(panel("Dp-1", "Serial"), false);
  CHECK(!overrides.isOff(panel("DP-1", "SERIAL")));
}

UMBRIEL_TEST(clearForgetsEveryDisconnectedChoice) {
  OutputPowerOverrides overrides;
  overrides.remember(panel("DP-1", "A"), true);
  overrides.remember(panel("DP-2", "B"), true);
  overrides.clear();
  CHECK(!overrides.isOff(panel("DP-1", "A")));
  CHECK(!overrides.isOff(panel("DP-2", "B")));
}

int main() { return RUN_TESTS(); }
