#include "../dxgi/dxgi_include.h"

#include "../util/config/config.h"

#include "dxvk_config.h"

namespace dxvk {
  Logger Logger::s_instance("dxvk_config.log");
}

static int32_t parsePciId(const std::string& str) {
  if (str.size() != 4)
    return -1;

  int32_t id = 0;

  for (size_t i = 0; i < str.size(); i++) {
    id *= 16;

    if (str[i] >= '0' && str[i] <= '9')
      id += str[i] - '0';
    else if (str[i] >= 'A' && str[i] <= 'F')
      id += str[i] - 'A' + 10;
    else if (str[i] >= 'a' && str[i] <= 'f')
      id += str[i] - 'a' + 10;
    else
      return -1;
  }

  return id;
}

extern "C" {
    using namespace dxvk;

    DLLEXPORT HRESULT __stdcall DXVKGetOptions(struct DXVKOptions *opts)
    {
        Config config(Config::getUserConfig());

        config.merge(Config::getAppConfig(env::getExePath()));

        opts->nvapiHack = config.getOption<bool>("dxgi.nvapiHack", true) ? 1 : 0;
        opts->customVendorId = parsePciId(config.getOption<std::string>("dxgi.customVendorId"));
        opts->customDeviceId = parsePciId(config.getOption<std::string>("dxgi.customDeviceId"));

        return S_OK;
    }
}
