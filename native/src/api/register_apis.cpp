#include "omni/api/register_apis.h"

#include "omni/api/api_dispatcher.h"

namespace omni {
namespace {

bool g_registered = false;

}  // namespace

void RegisterAllApis() {
  if (g_registered) {
    return;
  }
  g_registered = true;

  // Exact handlers first (adblock), then domain prefixes.
  RegisterAdblockApis();
  RegisterBrowserApis();
  RegisterWindowApis();
  RegisterLibraryApis();
  RegisterTerminalApis();
  RegisterGitApis();
}

}  // namespace omni
