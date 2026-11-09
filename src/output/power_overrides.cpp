#include "output/power_overrides.h"

#include <algorithm>

namespace umbriel {

  namespace {

    std::string descriptorFor(const OutputIdentity& identity) {
      if (identity.make.empty() && identity.model.empty() && identity.serial.empty()) {
        return {};
      }
      return outputDescriptor(identity);
    }

  } // namespace

  void OutputPowerOverrides::remember(const OutputIdentity& identity, bool off) {
    if (identity.connector.empty()) {
      return;
    }
    const std::string descriptor = descriptorFor(identity);
    std::erase_if(m_entries, [&](const Entry& entry) {
      return outputNamesEqual(entry.connector, identity.connector) && outputNamesEqual(entry.descriptor, descriptor);
    });
    if (off) {
      m_entries.push_back({.connector = std::string(identity.connector), .descriptor = descriptor});
    }
  }

  bool OutputPowerOverrides::isOff(const OutputIdentity& identity) const {
    const std::string descriptor = descriptorFor(identity);
    return std::ranges::any_of(m_entries, [&](const Entry& entry) {
      return outputNamesEqual(entry.connector, identity.connector) && outputNamesEqual(entry.descriptor, descriptor);
    });
  }

} // namespace umbriel
