#pragma once

#include <string>

namespace omni {

// Opens a Win32 "Open" dialog filtered to *.exe. Returns empty on cancel.
std::string PickExecutableDialog();

}  // namespace omni
