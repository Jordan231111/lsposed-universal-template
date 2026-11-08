#pragma once
#include "il2cpp_runtime.h"
#include <string>

namespace il2cpp {
// Optional process-lifetime adapter; no game methods are hooked or invoked.
bool start_observer(const std::string& library, std::string& error);
void stop_observer();
std::string observer_status();
Runtime* observed_runtime(); // nullptr until initialization and public metadata are verified.
}
