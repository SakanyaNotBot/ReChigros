// C++ side of the ObjectivelyMVC settings bridge.
//
// MvcSettings.c owns all framework types; this file only forwards to the C-ABI entry points
// it exports. Keeping the two apart means no C++ translation unit ever includes a framework
// header, which is what makes the MSVC tree able to build this screen's callers at all.

#include "rechigros/app/MvcSettings.hpp"

extern "C" {
bool rechigros_mvc_settings_available(void);
void rechigros_mvc_settings_shutdown(void);
}

namespace rechigros::app {

bool mvcSettingsAvailable() { return rechigros_mvc_settings_available(); }

void mvcSettingsShutdown() { rechigros_mvc_settings_shutdown(); }

} // namespace rechigros::app
