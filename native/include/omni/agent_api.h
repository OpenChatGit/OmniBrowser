#pragma once

#include <string>

namespace omni {

/** Register agent.* and page.* APIs on ApiDispatcher for LLM Agents and View automation. */
void RegisterAgentApis();

/** Fail every in-flight page.eval / page.extract so MCP does not hang on a crash. */
void FailPendingAgentQueries(const std::string& reason);

/** Drop the CDP session for a closed content browser. */
void DetachAgentDevTools(int browser_id);

}  // namespace omni
