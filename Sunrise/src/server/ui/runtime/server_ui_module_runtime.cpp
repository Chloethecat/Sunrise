#include "server_ui_module_runtime.h"

#include <string_view>
#include "../../../core/ui/modules/registry/ui_module_registry.h"
#include "../../../core/ui/modules/ui_module_descriptor.h"
#include "../activity_override/activity_override_panel.h"
#include "../world_state/world_state_panel.h"

namespace sunrise::server::ui::runtime {
namespace {
/** A namespaced stable ID keeps Server modules from clashing with Client modules. */
constexpr std::string_view kOverrideStableId = "server.activity_override";
constexpr std::string_view kWorldStateStableId = "server.world_state";
/** Short menu labels. */
constexpr std::string_view kOverrideDisplayName = "Activity";
constexpr std::string_view kWorldStateDisplayName = "World Flags";

core::ui::modules::registry::PageRegistration g_overridePage;
core::ui::modules::registry::PageRegistration g_worldStatePage;

} // namespace

/** @return True when every Server module owns its Core UI registry slot. */
bool initialize() noexcept {
    const bool activityOwned =
        g_overridePage.acquire(core::ui::modules::Owner::server,
                               kOverrideStableId,
                               kOverrideDisplayName,
                               &activity_override::draw);
    const bool worldStateOwned =
        g_worldStatePage.acquire(core::ui::modules::Owner::server,
                                 kWorldStateStableId,
                                 kWorldStateDisplayName,
                                 &world_state::draw);
    return activityOwned && worldStateOwned;
}

/** Removes the Server modules from the Core UI registry. */
void shutdown() noexcept {
    g_worldStatePage.release();
    g_overridePage.release();
}

} // namespace sunrise::server::ui::runtime
