#pragma once

#include "output/identity.h"

#include <string>
#include <vector>

namespace umbriel {

  // Session power requests survive backend recreation on the same connector
  // and display descriptor. Never migrate an off choice to another connector.
  class OutputPowerOverrides {
  public:
    void remember(const OutputIdentity& identity, bool off);
    bool isOff(const OutputIdentity& identity) const;
    void clear() { m_entries.clear(); }

  private:
    struct Entry {
      std::string connector;
      std::string descriptor;
    };

    std::vector<Entry> m_entries;
  };

} // namespace umbriel
