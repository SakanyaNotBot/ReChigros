#pragma once

namespace rechigros::app {

// Runs the headless end-to-end check: builds a temporary pack, imports it
// through the archive layer, scans the library, loads the chart and simulates
// gameplay frames without creating a window. Prints PASS/FAIL lines and returns
// 0 when every check passed, 1 otherwise.
int runSelfTest();

} // namespace rechigros::app
