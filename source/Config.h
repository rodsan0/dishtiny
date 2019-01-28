#pragma once

#include <string>

#include "base/vector.h"
#include "config/command_line.h"
#include "config/ArgManager.h"

#include "ConfigBase.h"
#include "ConfigLevel.h"

class Config : public ConfigBase {

private:
  emp::vector<ConfigLevel> clevs;

public:

  static constexpr size_t TAG_WIDTH = 16;

  using base_hardware_t = emp::EventDrivenGP_AW<TAG_WIDTH>;

  Config(emp::cl::ArgManager &args, std::string config_fname) {

    Read(config_fname);

    if (args.ProcessConfigOptions(*this, std::cout, config_fname,
          "../ConfigBase.h") == false)
      exit(0);
    if (args.TestUnknown() == false)
      exit(0);

    for (size_t l = 0; l < NLEV(); ++l) {
      clevs.push_back(ConfigLevel());

      if (l == 1) {
        clevs[l].Set("EVENT_RADIUS", "24.0");
      }
      clevs[l].Read(CONFIGLEVEL_BASENAME() + std::to_string(l) + CONFIGLEVEL_EXTENSION());
    }

  }

  ConfigLevel& Lev(size_t lev) {
    return clevs[lev];
  }

  void WriteMe(std::ostream & out) {
    Write(out);

    for (size_t l = 0; l < clevs.size(); ++l) {
      std::cout << std::endl << "ConfigLevel" << l << std::endl;
      clevs[l].Write(out);
    }

  }
};