#include "omni/mcp/mcp_server.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "omni/log.h"

#pragma comment(lib, "ws2_32.lib")

namespace omni {

namespace {

struct SseSession {
  SOCKET socket = INVALID_SOCKET;
  std::string id;
  std::mutex send_mu;
  bool active = true;
};

std::mutex g_sessions_mu;
std::map<std::string, std::shared_ptr<SseSession>> g_sse_sessions;

void SendAll(SOCKET s, const std::string& data) {
  int total = 0;
  int bytes_left = static_cast<int>(data.size());
  while (total < static_cast<int>(data.size())) {
    int n = send(s, data.data() + total, bytes_left, 0);
    if (n == SOCKET_ERROR || n == 0) {
      break;
    }
    total += n;
    bytes_left -= n;
  }
}

void SendHttpResponse(SOCKET client,
                      int status_code,
                      const std::string& status_text,
                      const std::string& content_type,
                      const std::string& body) {
  std::ostringstream oss;
  oss << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
  oss << "Content-Type: " << content_type << "\r\n";
  oss << "Content-Length: " << body.size() << "\r\n";
  oss << "Access-Control-Allow-Origin: *\r\n";
  oss << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
  oss << "Access-Control-Allow-Headers: *\r\n";
  oss << "Connection: close\r\n\r\n";
  oss << body;

  SendAll(client, oss.str());
}

std::wstring OmniAppDataDirW() {
  wchar_t appdata[MAX_PATH] = {};
  if (GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH) == 0) {
    return {};
  }
  return std::wstring(appdata) + L"\\OmniBrowser";
}

std::wstring EndpointFilePathW() {
  return OmniAppDataDirW() + L"\\mcp-endpoint.json";
}

bool ProcessAlive(DWORD pid) {
  if (pid == 0) {
    return false;
  }
  HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!proc) {
    return false;
  }
  DWORD exit_code = 0;
  const BOOL ok = GetExitCodeProcess(proc, &exit_code);
  CloseHandle(proc);
  return ok && exit_code == STILL_ACTIVE;
}

bool PathIsOmniBrowserExe(const wchar_t* path) {
  if (!path || !path[0]) {
    return false;
  }
  const wchar_t* slash = wcsrchr(path, L'\\');
  const wchar_t* name = slash ? slash + 1 : path;
  return _wcsicmp(name, L"OmniBrowser.exe") == 0;
}

bool PidIsOmniBrowser(DWORD pid) {
  HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!proc) {
    return false;
  }
  wchar_t path[MAX_PATH] = {};
  DWORD n = MAX_PATH;
  const BOOL ok = QueryFullProcessImageNameW(proc, 0, path, &n);
  CloseHandle(proc);
  return ok && PathIsOmniBrowserExe(path);
}

struct FindGuiState {
  DWORD skip_pid = 0;
  DWORD found_pid = 0;
};

BOOL CALLBACK FindOmniGuiWindowProc(HWND hwnd, LPARAM lparam) {
  auto* state = reinterpret_cast<FindGuiState*>(lparam);
  if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr) {
    return TRUE;
  }
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid == 0 || pid == state->skip_pid || pid == GetCurrentProcessId()) {
    return TRUE;
  }
  if (!PidIsOmniBrowser(pid)) {
    return TRUE;
  }
  state->found_pid = pid;
  return FALSE;
}

DWORD FindOmniGuiPid() {
  FindGuiState state;
  state.skip_pid = GetCurrentProcessId();
  EnumWindows(FindOmniGuiWindowProc, reinterpret_cast<LPARAM>(&state));
  return state.found_pid;
}

bool GuiMutexHeld() {
  HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, McpServer::kGuiMutexName);
  if (!mutex) {
    return false;
  }
  CloseHandle(mutex);
  return true;
}

void WriteEndpointFile(int port) {
  const std::wstring dir = OmniAppDataDirW();
  if (dir.empty()) {
    return;
  }
  CreateDirectoryW(dir.c_str(), nullptr);
  const std::wstring path = EndpointFilePathW();
  FILE* fp = nullptr;
  if (_wfopen_s(&fp, path.c_str(), L"wb") != 0 || !fp) {
    return;
  }
  std::fprintf(fp, "{\"port\":%d,\"pid\":%lu}\n", port,
               static_cast<unsigned long>(GetCurrentProcessId()));
  std::fclose(fp);
}

void ClearEndpointFile() {
  const std::wstring path = EndpointFilePathW();
  if (path.empty()) {
    return;
  }
  DeleteFileW(path.c_str());
}

bool ReadEndpointFile(int* port, DWORD* pid) {
  const std::wstring path = EndpointFilePathW();
  if (path.empty()) {
    return false;
  }
  std::ifstream in(path);
  if (!in) {
    return false;
  }
  std::string body((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  const auto grab = [&](const char* key, unsigned long* out) -> bool {
    const std::string needle = std::string("\"") + key + "\":";
    const size_t at = body.find(needle);
    if (at == std::string::npos) {
      return false;
    }
    *out = std::strtoul(body.c_str() + at + needle.size(), nullptr, 10);
    return *out != 0;
  };
  unsigned long p = 0;
  unsigned long process = 0;
  if (!grab("port", &p) || !grab("pid", &process)) {
    return false;
  }
  if (port) {
    *port = static_cast<int>(p);
  }
  if (pid) {
    *pid = static_cast<DWORD>(process);
  }
  return true;
}

bool GuiAlreadyOpen(DWORD* pid_out) {
  int port = 0;
  DWORD pid = 0;
  if (ReadEndpointFile(&port, &pid) && ProcessAlive(pid) &&
      PidIsOmniBrowser(pid)) {
    if (pid_out) {
      *pid_out = pid;
    }
    return true;
  }
  if (GuiMutexHeld()) {
    if (pid_out) {
      *pid_out = FindOmniGuiPid();
    }
    return true;
  }
  pid = FindOmniGuiPid();
  if (pid) {
    if (pid_out) {
      *pid_out = pid;
    }
    return true;
  }
  return false;
}

int ResolveLiveMcpPort(int fallback) {
  int port = 0;
  DWORD pid = 0;
  if (ReadEndpointFile(&port, &pid) && port > 0 && ProcessAlive(pid)) {
    return port;
  }
  return fallback;
}

void HandleClient(SOCKET client) {
  if (!McpServer::Get().IsHttpRunning() || McpServer::Get().IsShuttingDown()) {
    closesocket(client);
    return;
  }
  std::vector<char> buffer(65536);
  int received = recv(client, buffer.data(), static_cast<int>(buffer.size()) - 1, 0);
  if (received <= 0) {
    closesocket(client);
    return;
  }
  buffer[received] = '\0';
  std::string req_str(buffer.data(), received);

  // Parse HTTP Request Line
  std::istringstream stream(req_str);
  std::string method, full_path, proto;
  stream >> method >> full_path >> proto;

  if (method.empty() || full_path.empty()) {
    closesocket(client);
    return;
  }

  // CORS Preflight
  if (method == "OPTIONS") {
    std::string headers =
        "HTTP/1.1 204 No Content\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: *\r\n"
        "Access-Control-Max-Age: 86400\r\n"
        "Connection: close\r\n\r\n";
    SendAll(client, headers);
    closesocket(client);
    return;
  }

  std::string path = full_path;
  std::string query;
  size_t q_pos = full_path.find('?');
  if (q_pos != std::string::npos) {
    path = full_path.substr(0, q_pos);
    query = full_path.substr(q_pos + 1);
  }

  // Parse Headers & Body
  size_t body_pos = req_str.find("\r\n\r\n");
  std::string body;
  size_t content_length = 0;
  bool bad_length = false;

  std::string line;
  std::string agent_header;
  while (std::getline(stream, line) && line != "\r" && !line.empty()) {
    if (line.back() == '\r') line.pop_back();
    if (line.rfind("Content-Length:", 0) == 0 || line.rfind("content-length:", 0) == 0) {
      size_t colon = line.find(':');
      if (colon != std::string::npos) {
        const char* p = line.c_str() + colon + 1;
        while (*p == ' ' || *p == '\t') {
          ++p;
        }
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(p, &end, 10);
        if (end == p || parsed > 1024UL * 1024UL) {
          bad_length = true;
        } else {
          content_length = static_cast<size_t>(parsed);
        }
      }
    }
    std::string lower = line;
    for (char& c : lower) {
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    if (lower.rfind("x-omni-agent:", 0) == 0) {
      size_t colon = line.find(':');
      if (colon != std::string::npos) {
        agent_header = line.substr(colon + 1);
        while (!agent_header.empty() &&
               (agent_header.front() == ' ' || agent_header.front() == '\t')) {
          agent_header.erase(agent_header.begin());
        }
      }
    }
  }

  if (bad_length) {
    SendHttpResponse(client, 400, "Bad Request", "text/plain",
                     "Invalid or too large Content-Length");
    closesocket(client);
    return;
  }

  if (body_pos != std::string::npos) {
    body = req_str.substr(body_pos + 4);
    while (body.size() < content_length) {
      if (McpServer::Get().IsShuttingDown()) {
        break;
      }
      int extra = recv(client, buffer.data(), static_cast<int>(buffer.size()) - 1, 0);
      if (extra <= 0) break;
      body.append(buffer.data(), extra);
      if (body.size() > 1024 * 1024) {
        body.resize(1024 * 1024);
        break;
      }
    }
  }

  // 1. SSE Connection Endpoint: GET /sse or GET /mcp/sse
  if (method == "GET" && (path == "/sse" || path == "/mcp/sse" || path == "/mcp")) {
    const std::string session_id =
        "s_" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::system_clock::now().time_since_epoch())
                                  .count());

    std::ostringstream sse_hdr;
    sse_hdr << "HTTP/1.1 200 OK\r\n";
    sse_hdr << "Content-Type: text/event-stream\r\n";
    sse_hdr << "Cache-Control: no-cache\r\n";
    sse_hdr << "Connection: keep-alive\r\n";
    sse_hdr << "Access-Control-Allow-Origin: *\r\n";
    sse_hdr << "Access-Control-Allow-Headers: *\r\n\r\n";
    SendAll(client, sse_hdr.str());

    // Send initial endpoint event as required by MCP SSE Transport spec
    std::string endpoint_event = "event: endpoint\r\ndata: /message?sessionId=" + session_id + "\r\n\r\n";
    SendAll(client, endpoint_event);

    auto session = std::make_shared<SseSession>();
    session->socket = client;
    session->id = session_id;

    {
      std::lock_guard<std::mutex> lock(g_sessions_mu);
      g_sse_sessions[session_id] = session;
    }

    Log("McpServer: SSE Client connected: " + session_id);

    // Keep alive loop
    while (session->active) {
      std::this_thread::sleep_for(std::chrono::seconds(15));
      std::lock_guard<std::mutex> lock(session->send_mu);
      int res = send(client, ": ping\r\n\r\n", 9, 0);
      if (res == SOCKET_ERROR || res == 0) {
        break;
      }
    }

    {
      std::lock_guard<std::mutex> lock(g_sessions_mu);
      g_sse_sessions.erase(session_id);
    }
    closesocket(client);
    Log("McpServer: SSE Client disconnected: " + session_id);
    return;
  }

  // 2. Message Post Endpoint: POST /message
  if (method == "POST" && (path == "/message" || path == "/mcp/message")) {
    std::string session_id;
    size_t sid_pos = query.find("sessionId=");
    if (sid_pos != std::string::npos) {
      session_id = query.substr(sid_pos + 10);
      size_t amp = session_id.find('&');
      if (amp != std::string::npos) session_id = session_id.substr(0, amp);
    }

    const std::string agent_id = !agent_header.empty() ? agent_header : session_id;
    std::string response_json = McpServer::Get().ProcessJsonRpc(body, agent_id);

    // If session ID exists, deliver via SSE stream
    std::shared_ptr<SseSession> target_session;
    if (!session_id.empty()) {
      std::lock_guard<std::mutex> lock(g_sessions_mu);
      auto it = g_sse_sessions.find(session_id);
      if (it != g_sse_sessions.end()) {
        target_session = it->second;
      }
    }

    if (target_session && !response_json.empty()) {
      std::string sse_msg = "event: message\r\ndata: " + response_json + "\r\n\r\n";
      {
        std::lock_guard<std::mutex> lock(target_session->send_mu);
        SendAll(target_session->socket, sse_msg);
      }
      SendHttpResponse(client, 202, "Accepted", "text/plain", "Accepted");
    } else {
      SendHttpResponse(client, 200, "OK", "application/json", response_json);
    }

    closesocket(client);
    return;
  }

  // 3. Direct JSON-RPC POST Endpoint: POST / or POST /mcp or POST /api
  if (method == "POST") {
    std::string response_json =
        McpServer::Get().ProcessJsonRpc(body, agent_header);
    SendHttpResponse(client, 200, "OK", "application/json", response_json);
    closesocket(client);
    return;
  }

  // 4. Status and Discovery GET / or GET /api/status
  if (method == "GET") {
    Json status = {
        {"status", "running"},
        {"service", "OmniBrowser MCP & ACP Server"},
        {"version", "1.0.0"},
        {"protocols", {"mcp-2024-11-05", "acp-jsonrpc-2.0"}},
        {"endpoints",
         {{"sse", "/sse"},
          {"message", "/message?sessionId={id}"},
          {"direct_rpc", "/mcp"},
          {"status", "/status"}}}};
    SendHttpResponse(client, 200, "OK", "application/json", status.dump(2));
    closesocket(client);
    return;
  }

  SendHttpResponse(client, 404, "Not Found", "text/plain", "Not Found");
  closesocket(client);
}

}  // namespace

bool McpServer::StartHttpServer(int port) {
  if (http_running_) {
    return true;
  }
  Initialize();

  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    Log("McpServer: WSAStartup failed");
    return false;
  }

  SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (listen_sock == INVALID_SOCKET) {
    Log("McpServer: Failed to create socket");
    WSACleanup();
    return false;
  }

  int opt = 1;
  setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(static_cast<u_short>(port));

  int bind_ok = SOCKET_ERROR;
  for (int retry = 0; retry < 8; ++retry) {
    if (bind(listen_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != SOCKET_ERROR) {
      bind_ok = 0;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
  if (bind_ok == SOCKET_ERROR) {
    Log("McpServer: Failed to bind to 127.0.0.1:" + std::to_string(port) +
        " (WSAGetLastError=" + std::to_string(WSAGetLastError()) + ")");
    closesocket(listen_sock);
    WSACleanup();
    return false;
  }

  if (listen(listen_sock, SOMAXCONN) == SOCKET_ERROR) {
    Log("McpServer: Failed to listen on socket");
    closesocket(listen_sock);
    WSACleanup();
    return false;
  }

  http_port_ = port;
  listen_socket_ = static_cast<uintptr_t>(listen_sock);
  http_running_ = true;

  Log("McpServer: HTTP/SSE Server listening on http://127.0.0.1:" + std::to_string(port));
  WriteEndpointFile(port);

  http_thread_ = std::make_unique<std::thread>([this, listen_sock]() {
    while (http_running_) {
      sockaddr_in client_addr{};
      int client_len = sizeof(client_addr);
      SOCKET client = accept(listen_sock, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
      if (client == INVALID_SOCKET) {
        if (!http_running_) break;
        continue;
      }
      std::thread(HandleClient, client).detach();
    }
  });

  return true;
}

void McpServer::StopHttpServer() {
  if (!http_running_) {
    return;
  }
  http_running_ = false;
  if (listen_socket_ != ~0ULL) {
    closesocket(static_cast<SOCKET>(listen_socket_));
    listen_socket_ = ~0ULL;
  }
  if (http_thread_ && http_thread_->joinable()) {
    // Never join from the CEF UI thread: MCP tools block in RunOnUiAndWait
    // on this thread, so join() would deadlock shutdown.
    http_thread_->detach();
  }
  http_thread_.reset();

  {
    std::lock_guard<std::mutex> lock(g_sessions_mu);
    for (auto& [_, session] : g_sse_sessions) {
      session->active = false;
      if (session->socket != INVALID_SOCKET) {
        closesocket(session->socket);
      }
    }
    g_sse_sessions.clear();
  }

  WSACleanup();
  ClearEndpointFile();
  Log("McpServer: HTTP Server stopped.");
}

void McpServer::StartStdioServer() {
  if (stdio_running_) {
    return;
  }
  Initialize();
  stdio_running_ = true;

  Log("McpServer: Starting Stdio transport loop");

  stdio_thread_ = std::make_unique<std::thread>([this]() {
    const std::string stdio_agent =
        "stdio-" + std::to_string(::GetCurrentProcessId());
    std::string line;
    while (stdio_running_ && std::getline(std::cin, line)) {
      if (line.empty()) continue;
      std::string resp = ProcessJsonRpc(line, stdio_agent);
      if (!resp.empty()) {
        std::cout << resp << "\n";
        std::cout.flush();
      }
    }
    stdio_running_ = false;
  });
}

void McpServer::StopStdioServer() {
  if (!stdio_running_) {
    return;
  }
  stdio_running_ = false;
  if (stdio_thread_ && stdio_thread_->joinable()) {
    stdio_thread_->detach(); // Stdin getline can be blocking on Windows
  }
  stdio_thread_.reset();
}

bool McpServer::IsHttpReachable(int port, int timeout_ms) {
  if (port <= 0) {
    return false;
  }
  WSADATA wsaData;
  const bool started = WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;

  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) {
    if (started) {
      WSACleanup();
    }
    return false;
  }

  DWORD timeout = static_cast<DWORD>(std::max(80, timeout_ms));
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
             sizeof(timeout));
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout),
             sizeof(timeout));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<u_short>(port));
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

  if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) ==
      SOCKET_ERROR) {
    closesocket(s);
    if (started) {
      WSACleanup();
    }
    return false;
  }

  const char req[] =
      "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
  send(s, req, static_cast<int>(sizeof(req) - 1), 0);
  char buf[2048] = {};
  const int n = recv(s, buf, sizeof(buf) - 1, 0);
  closesocket(s);
  if (started) {
    WSACleanup();
  }
  if (n <= 0) {
    return GuiAlreadyOpen(nullptr);
  }
  const std::string raw(buf, buf + n);
  if (raw.find("OmniBrowser MCP") != std::string::npos ||
      raw.find("mcp-2024-11-05") != std::string::npos) {
    return true;
  }
  return GuiAlreadyOpen(nullptr);
}

namespace {

struct HttpRpcResult {
  bool ok = false;
  int winsock_error = 0;
  std::string body;
  std::string error;
};

std::string ReadRecentCrashFile() {
  wchar_t appdata[MAX_PATH] = {};
  if (GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH) == 0) {
    return {};
  }
  const std::wstring crash_path =
      std::wstring(appdata) + L"\\OmniBrowser\\last_crash.txt";
  WIN32_FILE_ATTRIBUTE_DATA attr{};
  if (GetFileAttributesExW(crash_path.c_str(), GetFileExInfoStandard, &attr)) {
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER a, b;
    a.LowPart = attr.ftLastWriteTime.dwLowDateTime;
    a.HighPart = attr.ftLastWriteTime.dwHighDateTime;
    b.LowPart = now.dwLowDateTime;
    b.HighPart = now.dwHighDateTime;
    const ULONGLONG age_s = (b.QuadPart - a.QuadPart) / 10000000ULL;
    if (age_s <= 180) {
      std::ifstream in(crash_path);
      std::string line;
      if (in && std::getline(in, line) && !line.empty()) {
        return line;
      }
    }
  }
  return {};
}

std::string LastCrashHint() {
  const std::string fresh = ReadRecentCrashFile();
  if (!fresh.empty()) {
    return fresh;
  }
  return {};
}

DWORD FindListenerPid(int port) {
  DWORD size = 0;
  GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER,
                      0);
  if (size == 0) {
    return 0;
  }
  std::vector<char> buf(size);
  auto* table = reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(buf.data());
  if (GetExtendedTcpTable(table, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER,
                          0) != NO_ERROR) {
    return 0;
  }
  const DWORD want = htons(static_cast<u_short>(port));
  for (DWORD i = 0; i < table->dwNumEntries; ++i) {
    const auto& row = table->table[i];
    if (row.dwLocalPort == want) {
      return row.dwOwningPid;
    }
  }
  return 0;
}

bool ProcessExited(HANDLE proc, DWORD* exit_code) {
  if (!proc || proc == INVALID_HANDLE_VALUE) {
    return false;
  }
  if (WaitForSingleObject(proc, 0) != WAIT_OBJECT_0) {
    return false;
  }
  if (exit_code) {
    if (!GetExitCodeProcess(proc, exit_code)) {
      *exit_code = 0xFFFFFFFF;
    }
  }
  return true;
}

Json ToolErrorResult(const Json& id, const std::string& message) {
  std::string text = message;
  const std::string crash = LastCrashHint();
  if (!crash.empty()) {
    text += "\nLast native crash log: ";
    text += crash;
  }
  return Json{
      {"jsonrpc", "2.0"},
      {"id", id},
      {"result",
       {{"content", {{{"type", "text"}, {"text", text}}}},
        {"isError", true}}}};
}

Json JsonRpcError(const Json& id, int code, const std::string& message) {
  return Json{{"jsonrpc", "2.0"},
              {"id", id},
              {"error", {{"code", code}, {"message", message}}}};
}

std::string ReadStdinLine(bool* eof) {
  HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
  if (!in || in == INVALID_HANDLE_VALUE) {
    if (eof) {
      *eof = true;
    }
    return {};
  }
  std::string line;
  char ch = 0;
  DWORD n = 0;
  for (;;) {
    const BOOL ok = ReadFile(in, &ch, 1, &n, nullptr);
    if (!ok || n == 0) {
      if (eof) {
        *eof = true;
      }
      break;
    }
    if (ch == '\n') {
      break;
    }
    if (ch != '\r') {
      line.push_back(ch);
    }
  }
  return line;
}

void WriteStdoutLine(const std::string& payload) {
  HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
  if (!out || out == INVALID_HANDLE_VALUE) {
    return;
  }
  DWORD n = 0;
  if (!payload.empty()) {
    WriteFile(out, payload.data(), static_cast<DWORD>(payload.size()), &n,
              nullptr);
  }
  WriteFile(out, "\n", 1, &n, nullptr);
  FlushFileBuffers(out);
}

bool LaunchBrowserGui() {
  DWORD existing = 0;
  if (GuiAlreadyOpen(&existing)) {
    Log("McpServer: GUI already open pid=" +
        std::to_string(static_cast<unsigned>(existing)) +
        ", not launching a second instance");
    return true;
  }
  wchar_t exe[MAX_PATH] = {};
  if (GetModuleFileNameW(nullptr, exe, MAX_PATH) == 0) {
    return false;
  }
  std::wstring dir(exe);
  const size_t slash = dir.find_last_of(L"\\/");
  if (slash != std::wstring::npos) {
    dir.resize(slash);
  }

  std::wstring cmd = L"\"";
  cmd += exe;
  cmd += L"\"";

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_SHOWNORMAL;
  PROCESS_INFORMATION pi{};

  DWORD flags =
      DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | CREATE_BREAKAWAY_FROM_JOB;
  BOOL ok = CreateProcessW(exe, cmd.data(), nullptr, nullptr, FALSE, flags,
                           nullptr, dir.c_str(), &si, &pi);
  if (!ok) {
    flags = DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP;
    ok = CreateProcessW(exe, cmd.data(), nullptr, nullptr, FALSE, flags,
                        nullptr, dir.c_str(), &si, &pi);
  }
  if (!ok) {
    return false;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}

HttpRpcResult HttpPostMcp(int port, const std::string& body,
                          const std::string& agent_id) {
  HttpRpcResult out;
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) {
    out.winsock_error = WSAGetLastError();
    out.error = "Could not create socket to OmniBrowser MCP (" +
                std::to_string(out.winsock_error) + ")";
    return out;
  }
  DWORD timeout = 30000;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
             sizeof(timeout));
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout),
             sizeof(timeout));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<u_short>(port));
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) ==
      SOCKET_ERROR) {
    out.winsock_error = WSAGetLastError();
    out.error = "OmniBrowser MCP is not reachable on 127.0.0.1:" +
                std::to_string(port) + " (WSA " +
                std::to_string(out.winsock_error) +
                "). The browser is closed or crashed.";
    closesocket(s);
    return out;
  }

  std::ostringstream req;
  req << "POST /mcp HTTP/1.1\r\n";
  req << "Host: 127.0.0.1:" << port << "\r\n";
  req << "Content-Type: application/json\r\n";
  req << "Content-Length: " << body.size() << "\r\n";
  if (!agent_id.empty()) {
    req << "X-Omni-Agent: " << agent_id << "\r\n";
  }
  req << "Connection: close\r\n\r\n";
  req << body;
  SendAll(s, req.str());

  const DWORD pid = FindListenerPid(port);
  HANDLE proc = nullptr;
  if (pid) {
    proc = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                       pid);
  }

  std::string raw;
  char buf[4096];
  int n = 0;
  int recv_err = 0;
  for (;;) {
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(s, &read_set);
    timeval tv{};
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    const int sel = select(0, &read_set, nullptr, nullptr, &tv);
    DWORD exit_code = 0;
    if (ProcessExited(proc, &exit_code)) {
      if (proc) {
        CloseHandle(proc);
      }
      closesocket(s);
      out.error = "OmniBrowser process exited during the tool call (PID " +
                  std::to_string(pid) + ", exit " + std::to_string(exit_code) +
                  "). This is a crash or forced close.";
      return out;
    }
    if (sel == 0) {
      continue;
    }
    if (sel == SOCKET_ERROR) {
      recv_err = WSAGetLastError();
      break;
    }
    n = recv(s, buf, sizeof(buf), 0);
    if (n > 0) {
      raw.append(buf, n);
      continue;
    }
    recv_err = (n == SOCKET_ERROR) ? WSAGetLastError() : 0;
    break;
  }
  if (proc) {
    CloseHandle(proc);
  }
  closesocket(s);

  const size_t sep = raw.find("\r\n\r\n");
  if (sep == std::string::npos) {
    out.winsock_error = recv_err;
    out.error =
        recv_err
            ? ("OmniBrowser closed the MCP connection during the call (WSA " +
               std::to_string(recv_err) + "). The app likely crashed.")
            : "OmniBrowser returned an empty MCP HTTP response. The app may "
              "have crashed or been closed.";
    return out;
  }
  out.body = raw.substr(sep + 4);
  while (!out.body.empty() &&
         (out.body.back() == '\n' || out.body.back() == '\r' ||
          out.body.back() == ' ')) {
    out.body.pop_back();
  }
  if (out.body.empty()) {
    out.error = "OmniBrowser returned an empty MCP body.";
    return out;
  }
  out.ok = true;
  return out;
}

bool g_gui_launched = false;
bool g_http_seen = false;
bool g_logged_attach = false;

bool WaitForMcpHttp(int* port, int timeout_ms) {
  const int fallback = *port;
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(std::max(200, timeout_ms));
  while (std::chrono::steady_clock::now() < deadline) {
    *port = ResolveLiveMcpPort(fallback);
    if (McpServer::IsHttpReachable(*port, 300)) {
      g_http_seen = true;
      return true;
    }
    Sleep(150);
  }
  *port = ResolveLiveMcpPort(fallback);
  if (McpServer::IsHttpReachable(*port, 400)) {
    g_http_seen = true;
    return true;
  }
  return false;
}

void LogAttachedGui(DWORD pid, int port) {
  if (g_logged_attach) {
    return;
  }
  g_logged_attach = true;
  Log("McpServer: attached to already-open OmniBrowser GUI pid=" +
      std::to_string(static_cast<unsigned>(pid)) + " port=" +
      std::to_string(port));
}

bool ConnectToBrowser(int* port, std::string* error) {
  *port = ResolveLiveMcpPort(*port);
  if (McpServer::IsHttpReachable(*port, 400)) {
    g_http_seen = true;
    DWORD pid = 0;
    GuiAlreadyOpen(&pid);
    LogAttachedGui(pid, *port);
    return true;
  }
  DWORD pid = 0;
  if (GuiAlreadyOpen(&pid)) {
    if (WaitForMcpHttp(port, 8000)) {
      LogAttachedGui(pid, *port);
      return true;
    }
    if (error) {
      *error =
          "OmniBrowser is already open (PID " +
          std::to_string(static_cast<unsigned>(pid)) +
          ") but MCP is not reachable on 127.0.0.1:" + std::to_string(*port) +
          ". The window may still be starting.";
    }
    return false;
  }
  if (error) {
    *error =
        "OmniBrowser is not running. Start the app yourself, or have an "
        "agent call a browser tool.";
  }
  return false;
}

bool EnsureBrowserForAgent(int* port, std::string* error) {
  if (ConnectToBrowser(port, nullptr)) {
    return true;
  }
  DWORD pid = 0;
  if (GuiAlreadyOpen(&pid)) {
    if (WaitForMcpHttp(port, 25000)) {
      LogAttachedGui(pid, *port);
      return true;
    }
    if (error) {
      *error =
          "OmniBrowser window is already open (PID " +
          std::to_string(static_cast<unsigned>(pid)) +
          ") but MCP did not come up on 127.0.0.1:" + std::to_string(*port) +
          " within 25s.";
    }
    return false;
  }
  if (!g_gui_launched) {
    if (!LaunchBrowserGui()) {
      if (error) {
        *error = "Failed to start OmniBrowser.exe";
      }
      return false;
    }
    g_gui_launched = true;
  }
  if (WaitForMcpHttp(port, 25000)) {
    return true;
  }
  if (error) {
    *error = "OmniBrowser MCP did not come up on 127.0.0.1:" +
             std::to_string(*port) +
             " within 25s. The window may have crashed on startup.";
  }
  return false;
}

}  // namespace

int McpServer::RunStdioHost(int port) {
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    return 1;
  }

  Get().Initialize();

  const std::string agent_id =
      "stdio-" + std::to_string(::GetCurrentProcessId());
  DWORD found_gui = 0;
  const int live = ResolveLiveMcpPort(port);
  if (GuiAlreadyOpen(&found_gui)) {
    Log("McpServer: stdio host detected already-open GUI pid=" +
        std::to_string(static_cast<unsigned>(found_gui)) +
        " mcp=127.0.0.1:" + std::to_string(live) + " as " + agent_id);
  } else {
    Log("McpServer: stdio host on MCP 2024-11-05, GUI MCP at 127.0.0.1:" +
        std::to_string(port) + " as " + agent_id);
  }

  for (;;) {
    bool eof = false;
    const std::string line = ReadStdinLine(&eof);
    if (eof && line.empty()) {
      break;
    }
    if (line.empty()) {
      continue;
    }

    Json req = Json::parse(line, nullptr, false);
    if (req.is_discarded() || !req.is_object()) {
      WriteStdoutLine(JsonRpcError(nullptr, -32700, "Parse error").dump());
      continue;
    }

    const Json id = req.contains("id") ? req["id"] : Json(nullptr);
    const std::string method = req.value("method", "");
    const bool is_notification = !req.contains("id");

    if (method == "initialize") {
      WriteStdoutLine(Get().HandleJsonRpcRequest(req, agent_id).dump());
      continue;
    }
    if (method == "notifications/initialized" ||
        method == "notifications/cancelled") {
      continue;
    }
    if (method == "ping" || method == "logging/setLevel" ||
        method == "tools/list" || method == "resources/list" ||
        method == "prompts/list" || method == "prompts/get") {
      const Json resp = Get().HandleJsonRpcRequest(req, agent_id);
      if (!is_notification && !resp.empty()) {
        WriteStdoutLine(resp.dump());
      }
      continue;
    }

    if (is_notification) {
      continue;
    }

    std::string ensure_error;
    int live_port = ResolveLiveMcpPort(port);
    DWORD gui_pid = 0;
    const bool gui_open = GuiAlreadyOpen(&gui_pid);
    bool connected = ConnectToBrowser(&live_port, nullptr);
    if (!connected && method == "tools/call") {
      if (gui_open) {
        connected = WaitForMcpHttp(&live_port, 20000);
        if (connected) {
          LogAttachedGui(gui_pid, live_port);
        } else {
          ensure_error =
              "OmniBrowser is already open (PID " +
              std::to_string(static_cast<unsigned>(gui_pid)) +
              ") but MCP is not reachable on 127.0.0.1:" +
              std::to_string(live_port) + ".";
        }
      } else if (g_http_seen && !GuiAlreadyOpen(nullptr)) {
        ensure_error =
            "OmniBrowser was running and then disappeared (crash or close). "
            "Not restarting it automatically. Start the app yourself.";
      } else {
        connected = EnsureBrowserForAgent(&live_port, &ensure_error);
      }
    } else if (!connected) {
      ConnectToBrowser(&live_port, &ensure_error);
    }
    if (!connected) {
      const Json params = req.value("params", Json::object());
      const std::string tool_name = params.value("name", "");
      if (method == "tools/call" && tool_name == "browser_status") {
        Json status = {
            {"ok", false},
            {"ready", false},
            {"alreadyOpen", gui_open || GuiAlreadyOpen(nullptr)},
            {"mcpReachable", false},
            {"pid", static_cast<int>(gui_pid)},
            {"port", live_port},
        };
        WriteStdoutLine(
            Json{{"jsonrpc", "2.0"},
                 {"id", id},
                 {"result",
                  {{"content",
                    {{{"type", "text"}, {"text", status.dump(2)}}}},
                   {"isError", false}}}}
                .dump());
        continue;
      }
      if (method == "tools/call") {
        WriteStdoutLine(ToolErrorResult(id, ensure_error).dump());
      } else {
        WriteStdoutLine(JsonRpcError(id, -32000, ensure_error).dump());
      }
      continue;
    }

    const HttpRpcResult rpc = HttpPostMcp(live_port, line, agent_id);
    if (!rpc.ok) {
      std::string err = rpc.error;
      if (gui_open || GuiAlreadyOpen(nullptr)) {
        err = "OmniBrowser is open but the MCP call failed. " + rpc.error;
      }
      if (method == "tools/call") {
        WriteStdoutLine(ToolErrorResult(id, err).dump());
      } else {
        WriteStdoutLine(JsonRpcError(id, -32000, err).dump());
      }
      continue;
    }

    Json proxied = Json::parse(rpc.body, nullptr, false);
    if (proxied.is_discarded()) {
      if (method == "tools/call") {
        WriteStdoutLine(
            ToolErrorResult(id, "OmniBrowser returned invalid JSON: " + rpc.body)
                .dump());
      } else {
        WriteStdoutLine(
            JsonRpcError(id, -32603, "Invalid JSON from OmniBrowser").dump());
      }
      continue;
    }
    WriteStdoutLine(proxied.dump());
  }

  WSACleanup();
  return 0;
}

int McpServer::RunStdioProxy(int port) {
  return RunStdioHost(port);
}

}  // namespace omni
