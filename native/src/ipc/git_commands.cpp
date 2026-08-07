#include "omni/git_commands.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>

#include "include/cef_task.h"
#include "include/wrapper/cef_helpers.h"
#include "omni/paths.h"
#include "omni/utf8.h"

namespace omni {
namespace {

class UiTask : public CefTask {
 public:
  explicit UiTask(std::function<void()> fn) : fn_(std::move(fn)) {}
  void Execute() override {
    if (fn_) {
      fn_();
    }
  }

 private:
  std::function<void()> fn_;
  IMPLEMENT_REFCOUNTING(UiTask);
};

std::wstring FindGitExe() {
  wchar_t buf[MAX_PATH];
  if (SearchPathW(nullptr, L"git.exe", nullptr, MAX_PATH, buf, nullptr) > 0) {
    return buf;
  }
  const wchar_t* candidates[] = {
      L"C:\\Program Files\\Git\\cmd\\git.exe",
      L"C:\\Program Files\\Git\\bin\\git.exe",
      L"C:\\Program Files (x86)\\Git\\cmd\\git.exe",
  };
  for (const wchar_t* path : candidates) {
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
      return path;
    }
  }
  return L"git.exe";
}

bool RunGit(const std::wstring& git_exe,
            const std::wstring& args,
            const std::wstring& cwd,
            std::string* stdout_out,
            std::string* stderr_out,
            DWORD* exit_code) {
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  HANDLE out_read = INVALID_HANDLE_VALUE;
  HANDLE out_write = INVALID_HANDLE_VALUE;
  HANDLE err_read = INVALID_HANDLE_VALUE;
  HANDLE err_write = INVALID_HANDLE_VALUE;
  if (!CreatePipe(&out_read, &out_write, &sa, 0) ||
      !CreatePipe(&err_read, &err_write, &sa, 0)) {
    return false;
  }
  SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(err_read, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  si.hStdOutput = out_write;
  si.hStdError = err_write;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

  std::wstring cmdline = L"\"" + git_exe + L"\" " + args;
  std::vector<wchar_t> mutable_cmd(cmdline.begin(), cmdline.end());
  mutable_cmd.push_back(L'\0');

  PROCESS_INFORMATION pi{};
  const BOOL ok = CreateProcessW(
      git_exe.c_str(), mutable_cmd.data(), nullptr, nullptr, TRUE,
      CREATE_NO_WINDOW, nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);

  CloseHandle(out_write);
  CloseHandle(err_write);

  if (!ok) {
    CloseHandle(out_read);
    CloseHandle(err_read);
    return false;
  }

  auto read_all = [](HANDLE h) -> std::string {
    std::string out;
    char buf[4096];
    for (;;) {
      DWORD n = 0;
      if (!ReadFile(h, buf, sizeof(buf), &n, nullptr) || n == 0) {
        break;
      }
      out.append(buf, buf + n);
    }
    return out;
  };

  // Read stdout/stderr concurrently — sequential reads can deadlock when both
  // pipes fill their buffers.
  std::string so;
  std::string se;
  std::thread t_out([&]() { so = read_all(out_read); });
  std::thread t_err([&]() { se = read_all(err_read); });
  t_out.join();
  t_err.join();
  CloseHandle(out_read);
  CloseHandle(err_read);

  WaitForSingleObject(pi.hProcess, 15000);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);

  if (stdout_out) {
    *stdout_out = so;
  }
  if (stderr_out) {
    *stderr_out = se;
  }
  if (exit_code) {
    *exit_code = code;
  }
  return true;
}

std::string Trim(const std::string& s) {
  size_t a = 0;
  while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' ||
                          s[a] == '\n')) {
    ++a;
  }
  size_t b = s.size();
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' ||
                   s[b - 1] == '\n')) {
    --b;
  }
  return s.substr(a, b - a);
}

std::string FindRepoRoot(const std::string& start) {
  std::error_code ec;
  std::filesystem::path cur(utf8::Widen(start));
  cur = std::filesystem::weakly_canonical(cur, ec);
  if (ec) {
    cur = std::filesystem::path(utf8::Widen(start));
  }
  if (std::filesystem::is_regular_file(cur, ec)) {
    cur = cur.parent_path();
  }
  while (!cur.empty()) {
    if (std::filesystem::exists(cur / ".git", ec)) {
      return utf8::Narrow(cur.wstring());
    }
    const auto parent = cur.parent_path();
    if (parent == cur) {
      break;
    }
    cur = parent;
  }
  return {};
}

std::string DefaultRepoHint() {
  // Dev: ui/ lives in the repo → parent is the git root.
  const std::filesystem::path ui(utf8::Widen(paths::UiRootDir()));
  return utf8::Narrow(ui.parent_path().wstring());
}

int CountFileLines(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return 0;
  }
  int lines = 0;
  std::string line;
  constexpr std::uintmax_t kMax = 4ull * 1024ull * 1024ull;
  std::uintmax_t bytes = 0;
  while (std::getline(in, line)) {
    ++lines;
    bytes += static_cast<std::uintmax_t>(line.size()) + 1;
    if (bytes > kMax) {
      break;
    }
  }
  return lines;
}

struct FileEntry {
  std::string path;
  std::string status;  // M, A, D, R, U (untracked), C (copied)
  bool untracked = false;
  int additions = 0;
  int deletions = 0;
};

void ApplyNumstat(std::vector<FileEntry>& files, const std::string& numstat) {
  std::istringstream ss(numstat);
  std::string line;
  while (std::getline(ss, line)) {
    line = Trim(line);
    if (line.empty()) {
      continue;
    }
    // additions \t deletions \t path
    const size_t t1 = line.find('\t');
    if (t1 == std::string::npos) {
      continue;
    }
    const size_t t2 = line.find('\t', t1 + 1);
    if (t2 == std::string::npos) {
      continue;
    }
    const std::string add_s = line.substr(0, t1);
    const std::string del_s = line.substr(t1 + 1, t2 - t1 - 1);
    std::string path = line.substr(t2 + 1);
    // renames: old => new
    const size_t arrow = path.find(" => ");
    if (arrow != std::string::npos) {
      path = path.substr(arrow + 4);
    }
    int add = 0;
    int del = 0;
    if (add_s != "-") {
      add = std::atoi(add_s.c_str());
    }
    if (del_s != "-") {
      del = std::atoi(del_s.c_str());
    }
    for (auto& f : files) {
      if (f.path == path) {
        f.additions += add;
        f.deletions += del;
        break;
      }
    }
  }
}

Json CollectStatus(const std::string& hint) {
  Json out = Json::object();
  const std::string start = hint.empty() ? DefaultRepoHint() : hint;
  const std::string root = FindRepoRoot(start);
  out["root"] = root;
  if (root.empty()) {
    out["ok"] = false;
    out["error"] = "Not a git repository";
    out["branch"] = "";
    out["files"] = Json::array();
    out["additions"] = 0;
    out["deletions"] = 0;
    return out;
  }

  const std::wstring git = FindGitExe();
  const std::wstring cwd = utf8::Widen(root);
  std::string so;
  std::string se;
  DWORD code = 1;

  if (!RunGit(git, L"rev-parse --abbrev-ref HEAD", cwd, &so, &se, &code) ||
      code != 0) {
    out["ok"] = false;
    out["error"] = Trim(se).empty() ? "git not available" : Trim(se);
    out["branch"] = "";
    out["files"] = Json::array();
    out["additions"] = 0;
    out["deletions"] = 0;
    return out;
  }
  const std::string branch = Trim(so);

  so.clear();
  se.clear();
  if (!RunGit(git, L"status --porcelain=v1 -uall", cwd, &so, &se, &code)) {
    out["ok"] = false;
    out["error"] = "git status failed";
    out["branch"] = branch;
    out["files"] = Json::array();
    out["additions"] = 0;
    out["deletions"] = 0;
    return out;
  }

  std::vector<FileEntry> files;
  {
    std::istringstream ss(so);
    std::string line;
    while (std::getline(ss, line)) {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      if (line.size() < 4) {
        continue;
      }
      const char x = line[0];
      const char y = line[1];
      std::string path = line.substr(3);
      // rename: "R  old -> new" / "R  old -> new"
      const size_t arrow = path.find(" -> ");
      if (arrow != std::string::npos) {
        path = path.substr(arrow + 4);
      }
      if (!path.empty() && path.front() == '"' && path.back() == '"') {
        path = path.substr(1, path.size() - 2);
      }

      FileEntry fe;
      fe.path = path;
      fe.untracked = (x == '?' && y == '?');
      if (fe.untracked) {
        fe.status = "U";
      } else if (x == 'A' || y == 'A') {
        fe.status = "A";
      } else if (x == 'D' || y == 'D') {
        fe.status = "D";
      } else if (x == 'R' || y == 'R') {
        fe.status = "R";
      } else {
        fe.status = "M";
      }
      files.push_back(std::move(fe));
    }
  }

  // Line stats for tracked changes vs HEAD.
  so.clear();
  se.clear();
  RunGit(git, L"diff --numstat HEAD", cwd, &so, &se, &code);
  ApplyNumstat(files, so);

  // Untracked: count lines on disk.
  const std::filesystem::path root_path(utf8::Widen(root));
  for (auto& f : files) {
    if (!f.untracked) {
      continue;
    }
    f.additions = CountFileLines(root_path / utf8::Widen(f.path));
    f.deletions = 0;
  }

  int total_add = 0;
  int total_del = 0;
  Json arr = Json::array();
  for (const auto& f : files) {
    total_add += f.additions;
    total_del += f.deletions;
    Json item = Json::object();
    item["path"] = f.path;
    item["status"] = f.status;
    item["untracked"] = f.untracked;
    item["additions"] = f.additions;
    item["deletions"] = f.deletions;
    arr.push_back(item);
  }

  out["ok"] = true;
  out["error"] = "";
  out["branch"] = branch;
  out["files"] = arr;
  out["additions"] = total_add;
  out["deletions"] = total_del;
  return out;
}

}  // namespace

bool HandleGitCommand(
    const std::string& method,
    const Json& params,
    CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) {
  if (method.rfind("git.", 0) != 0) {
    return false;
  }

  if (method == "git.status") {
    const std::string cwd = params.value("cwd", "");
    std::thread([callback, cwd]() {
      const Json result = CollectStatus(cwd);
      CefPostTask(TID_UI, new UiTask([callback, result]() {
                    callback->Success(result.dump());
                  }));
    }).detach();
    return true;
  }

  callback->Failure(404, "Unknown method: " + method);
  return true;
}

}  // namespace omni
