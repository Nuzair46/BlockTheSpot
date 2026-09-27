#include "../../include/blockthespot_mod.h"
#include "../../Common/windows.h"

// The host owns the patch engine; this mod supplies its own rules and preferences.
// The same registration API is available to every DLL mod.
extern "C" __declspec(dllexport) int __cdecl bts_mod_init(const BtsModHost* host) {
    if (!host || host->api_version != BTS_MOD_API_VERSION || host->size < sizeof(BtsModHost) ||
        !host->register_ini || !host->config_path) return 0;
    std::string source;
    if (!bts::read_text(host->config_path, source)) {
        host->log(host->log_context, 0, "cannot read the companion BlockTheSpot INI");
        return 0;
    }
    return host->register_ini(host->log_context, source.data(), source.size());
}
