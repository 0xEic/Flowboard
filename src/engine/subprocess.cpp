// SPDX-License-Identifier: MIT
#include "engine/subprocess.hpp"

#include <spdlog/spdlog.h>

#include <chrono>
#include <cstring>
#include <thread>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <cerrno>
#  include <csignal>
#  include <fcntl.h>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

namespace flowboard {

#if defined(_WIN32)

struct Subprocess::Impl {
    HANDLE proc        = nullptr;
    HANDLE stdin_write = nullptr;
    HANDLE stdout_read = nullptr;
    HANDLE stderr_read = nullptr;
    DWORD  pid         = 0;
    std::string stdout_buf;
    std::string stderr_buf;

    ~Impl() {
        if (stdin_write) CloseHandle(stdin_write);
        if (stdout_read) CloseHandle(stdout_read);
        if (stderr_read) CloseHandle(stderr_read);
        if (proc) {
            // Safety net: don't let the child outlive the Subprocess object.
            TerminateProcess(proc, 1);
            WaitForSingleObject(proc, 1000);
            CloseHandle(proc);
        }
    }
};

// Quote one argv element per Microsoft's CommandLineToArgvW rules
// (https://learn.microsoft.com/en-us/cpp/cpp/main-function-command-line-args).
static void append_quoted(std::string& cmdline, std::string_view arg) {
    bool need_quotes = arg.empty() || arg.find_first_of(" \t\"\n\v") != std::string::npos;
    if (!need_quotes) { cmdline.append(arg); return; }
    cmdline.push_back('"');
    for (auto it = arg.begin(); it != arg.end(); ) {
        std::size_t backslashes = 0;
        while (it != arg.end() && *it == '\\') { ++backslashes; ++it; }
        if (it == arg.end()) {
            cmdline.append(backslashes * 2, '\\');
        } else if (*it == '"') {
            cmdline.append(backslashes * 2 + 1, '\\');
            cmdline.push_back('"');
            ++it;
        } else {
            cmdline.append(backslashes, '\\');
            cmdline.push_back(*it);
            ++it;
        }
    }
    cmdline.push_back('"');
}

static std::wstring utf8_to_wide(std::string_view s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::unique_ptr<Subprocess> Subprocess::spawn(std::filesystem::path const& exe,
                                                std::vector<std::string> const& args) {
    auto self = std::unique_ptr<Subprocess>(new Subprocess);
    auto& impl = *self->impl_;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE in_read = nullptr, out_write = nullptr, err_write = nullptr;
    if (!CreatePipe(&in_read, &impl.stdin_write, &sa, 0) ||
        !CreatePipe(&impl.stdout_read, &out_write, &sa, 0) ||
        !CreatePipe(&impl.stderr_read, &err_write, &sa, 0)) {
        spdlog::error("subprocess: CreatePipe failed ({})", GetLastError());
        return nullptr;
    }
    // The parent's ends must NOT be inheritable by the child.
    SetHandleInformation(impl.stdin_write, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(impl.stdout_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(impl.stderr_read, HANDLE_FLAG_INHERIT, 0);

    std::string cmd;
    append_quoted(cmd, exe.string());
    for (auto const& a : args) { cmd.push_back(' '); append_quoted(cmd, a); }
    auto wcmd = utf8_to_wide(cmd);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = in_read;
    si.hStdOutput = out_write;
    si.hStdError  = err_write;
    PROCESS_INFORMATION pi{};

    // lpApplicationName must be null so CreateProcessW searches PATH using the
    // first token of the command line (and applies PATHEXT for bare names like
    // "python"). Passing the exe as lpApplicationName disables that lookup.
    BOOL ok = CreateProcessW(
        nullptr,
        wcmd.data(),
        nullptr, nullptr,
        TRUE,              // inherit handles
        CREATE_NO_WINDOW,
        nullptr, nullptr,
        &si, &pi);

    CloseHandle(in_read);
    CloseHandle(out_write);
    CloseHandle(err_write);

    if (!ok) {
        spdlog::error("subprocess: CreateProcessW({}) failed ({})", exe.string(), GetLastError());
        return nullptr;
    }
    CloseHandle(pi.hThread);
    impl.proc = pi.hProcess;
    impl.pid  = pi.dwProcessId;
    return self;
}

bool Subprocess::write_line(std::string_view line) {
    if (!impl_->stdin_write) return false;
    auto write_all = [&](char const* p, std::size_t n) {
        while (n > 0) {
            DWORD w = 0;
            if (!WriteFile(impl_->stdin_write, p, (DWORD)n, &w, nullptr) || w == 0) return false;
            p += w; n -= w;
        }
        return true;
    };
    if (!write_all(line.data(), line.size())) return false;
    if (line.empty() || line.back() != '\n') { char nl = '\n'; if (!write_all(&nl, 1)) return false; }
    return true;
}

static bool read_line_into(HANDLE h, std::string& buf, std::string& out) {
    while (true) {
        auto nl = buf.find('\n');
        if (nl != std::string::npos) {
            out.assign(buf, 0, nl);
            if (!out.empty() && out.back() == '\r') out.pop_back();
            buf.erase(0, nl + 1);
            return true;
        }
        char chunk[4096];
        DWORD n = 0;
        if (!ReadFile(h, chunk, sizeof(chunk), &n, nullptr) || n == 0) {
            if (!buf.empty()) { out = std::move(buf); buf.clear(); return true; }
            return false;
        }
        buf.append(chunk, n);
    }
}

bool Subprocess::read_stdout_line(std::string& out) {
    return impl_->stdout_read && read_line_into(impl_->stdout_read, impl_->stdout_buf, out);
}
bool Subprocess::read_stderr_line(std::string& out) {
    return impl_->stderr_read && read_line_into(impl_->stderr_read, impl_->stderr_buf, out);
}

void Subprocess::close_stdin() {
    if (impl_->stdin_write) { CloseHandle(impl_->stdin_write); impl_->stdin_write = nullptr; }
}

void Subprocess::terminate() {
    if (impl_->proc) TerminateProcess(impl_->proc, 1);
}

std::optional<int> Subprocess::wait(std::chrono::milliseconds timeout) {
    if (!impl_->proc) return std::nullopt;
    DWORD ms = timeout.count() < 0 ? INFINITE : (DWORD)timeout.count();
    if (WaitForSingleObject(impl_->proc, ms) != WAIT_OBJECT_0) return std::nullopt;
    DWORD code = 0;
    GetExitCodeProcess(impl_->proc, &code);
    return (int)code;
}

bool Subprocess::is_running() {
    if (!impl_->proc) return false;
    return WaitForSingleObject(impl_->proc, 0) == WAIT_TIMEOUT;
}

#else  // POSIX

struct Subprocess::Impl {
    pid_t pid       = -1;
    int   stdin_fd  = -1;
    int   stdout_fd = -1;
    int   stderr_fd = -1;
    std::string stdout_buf;
    std::string stderr_buf;
    bool exited    = false;
    int  exit_code = 0;

    ~Impl() {
        if (stdin_fd  >= 0) ::close(stdin_fd);
        if (stdout_fd >= 0) ::close(stdout_fd);
        if (stderr_fd >= 0) ::close(stderr_fd);
        if (pid > 0 && !exited) { ::kill(pid, SIGTERM); ::waitpid(pid, nullptr, 0); }
    }
};

std::unique_ptr<Subprocess> Subprocess::spawn(std::filesystem::path const& exe,
                                                std::vector<std::string> const& args) {
    auto self = std::unique_ptr<Subprocess>(new Subprocess);
    auto& impl = *self->impl_;

    int in_pipe[2]{-1,-1}, out_pipe[2]{-1,-1}, err_pipe[2]{-1,-1};
    if (::pipe(in_pipe) != 0 || ::pipe(out_pipe) != 0 || ::pipe(err_pipe) != 0) {
        spdlog::error("subprocess: pipe() failed ({})", std::strerror(errno));
        for (int fd : {in_pipe[0],in_pipe[1],out_pipe[0],out_pipe[1],err_pipe[0],err_pipe[1]})
            if (fd >= 0) ::close(fd);
        return nullptr;
    }

    std::vector<std::string> argv_storage;
    argv_storage.reserve(args.size() + 1);
    argv_storage.push_back(exe.string());
    for (auto const& a : args) argv_storage.push_back(a);

    std::vector<char*> argv;
    argv.reserve(argv_storage.size() + 1);
    for (auto& s : argv_storage) argv.push_back(s.data());
    argv.push_back(nullptr);

    pid_t pid = ::fork();
    if (pid < 0) {
        spdlog::error("subprocess: fork() failed ({})", std::strerror(errno));
        for (int fd : {in_pipe[0],in_pipe[1],out_pipe[0],out_pipe[1],err_pipe[0],err_pipe[1]})
            ::close(fd);
        return nullptr;
    }
    if (pid == 0) {
        ::dup2(in_pipe[0],  STDIN_FILENO);
        ::dup2(out_pipe[1], STDOUT_FILENO);
        ::dup2(err_pipe[1], STDERR_FILENO);
        for (int fd : {in_pipe[0],in_pipe[1],out_pipe[0],out_pipe[1],err_pipe[0],err_pipe[1]})
            ::close(fd);
        ::execvp(argv[0], argv.data());
        ::dprintf(STDERR_FILENO, "subprocess: execvp(%s) failed: %s\n",
                  argv[0], std::strerror(errno));
        ::_exit(127);
    }

    ::close(in_pipe[0]);
    ::close(out_pipe[1]);
    ::close(err_pipe[1]);
    impl.pid       = pid;
    impl.stdin_fd  = in_pipe[1];
    impl.stdout_fd = out_pipe[0];
    impl.stderr_fd = err_pipe[0];
    return self;
}

bool Subprocess::write_line(std::string_view line) {
    if (impl_->stdin_fd < 0) return false;
    auto write_all = [&](char const* p, std::size_t n) {
        while (n > 0) {
            ssize_t w = ::write(impl_->stdin_fd, p, n);
            if (w < 0) { if (errno == EINTR) continue; return false; }
            p += w; n -= (std::size_t)w;
        }
        return true;
    };
    if (!write_all(line.data(), line.size())) return false;
    if (line.empty() || line.back() != '\n') { char nl = '\n'; if (!write_all(&nl, 1)) return false; }
    return true;
}

static bool read_line_fd(int fd, std::string& buf, std::string& out) {
    while (true) {
        auto nl = buf.find('\n');
        if (nl != std::string::npos) {
            out.assign(buf, 0, nl);
            if (!out.empty() && out.back() == '\r') out.pop_back();
            buf.erase(0, nl + 1);
            return true;
        }
        char chunk[4096];
        ssize_t n = ::read(fd, chunk, sizeof(chunk));
        if (n < 0) { if (errno == EINTR) continue; return false; }
        if (n == 0) {
            if (!buf.empty()) { out = std::move(buf); buf.clear(); return true; }
            return false;
        }
        buf.append(chunk, (std::size_t)n);
    }
}

bool Subprocess::read_stdout_line(std::string& out) {
    return impl_->stdout_fd >= 0 && read_line_fd(impl_->stdout_fd, impl_->stdout_buf, out);
}
bool Subprocess::read_stderr_line(std::string& out) {
    return impl_->stderr_fd >= 0 && read_line_fd(impl_->stderr_fd, impl_->stderr_buf, out);
}

void Subprocess::close_stdin() {
    if (impl_->stdin_fd >= 0) { ::close(impl_->stdin_fd); impl_->stdin_fd = -1; }
}

void Subprocess::terminate() {
    if (impl_->pid > 0 && !impl_->exited) ::kill(impl_->pid, SIGKILL);
}

std::optional<int> Subprocess::wait(std::chrono::milliseconds timeout) {
    if (impl_->pid <= 0) return std::nullopt;
    if (impl_->exited) return impl_->exit_code;
    auto deadline = std::chrono::steady_clock::now() + timeout;
    int status = 0;
    while (true) {
        pid_t r = ::waitpid(impl_->pid, &status, WNOHANG);
        if (r == impl_->pid) {
            impl_->exited = true;
            if (WIFEXITED(status))         impl_->exit_code = WEXITSTATUS(status);
            else if (WIFSIGNALED(status))  impl_->exit_code = 128 + WTERMSIG(status);
            else                           impl_->exit_code = -1;
            return impl_->exit_code;
        }
        if (r < 0) return std::nullopt;
        if (std::chrono::steady_clock::now() >= deadline) return std::nullopt;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

bool Subprocess::is_running() {
    if (impl_->pid <= 0 || impl_->exited) return false;
    int status = 0;
    pid_t r = ::waitpid(impl_->pid, &status, WNOHANG);
    if (r == 0) return true;
    if (r == impl_->pid) {
        impl_->exited = true;
        if (WIFEXITED(status))        impl_->exit_code = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) impl_->exit_code = 128 + WTERMSIG(status);
        else                          impl_->exit_code = -1;
    }
    return false;
}

#endif

Subprocess::Subprocess() : impl_(std::make_unique<Impl>()) {}
Subprocess::~Subprocess() = default;

}  // namespace flowboard
