#pragma once

namespace omni {

void RegisterAdblockApis();
void RegisterBrowserApis();
void RegisterWindowApis();
void RegisterLibraryApis();
void RegisterTerminalApis();
void RegisterGitApis();
void RegisterPlatformApis();
void RegisterAgentApis();

/** Idempotent: wires every domain into ApiDispatcher::Get(). */
void RegisterAllApis();

}  // namespace omni
