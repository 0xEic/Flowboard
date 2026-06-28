// SPDX-License-Identifier: MIT
#pragma once
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// \file
/// \brief Cross-platform child-process launcher with bidirectional stdio pipes.
///
/// Engine-internal — used by Python.Script to host a Python worker process.

namespace flowboard {

class Subprocess {
public:
    /// Launch \p exe with \p args. Returns nullptr on failure (and logs why).
    /// Stdin/stdout/stderr of the child are connected to pipes the caller
    /// reads/writes via the methods below.
    static std::unique_ptr<Subprocess> spawn(std::filesystem::path const& exe,
                                              std::vector<std::string> const& args);

    ~Subprocess();
    Subprocess(Subprocess const&) = delete;
    Subprocess& operator=(Subprocess const&) = delete;

    /// Write a single line to the child's stdin (a trailing newline is added if
    /// missing). Returns false on broken pipe.
    bool write_line(std::string_view line);

    /// Read one newline-terminated line from the child's stdout into \p out.
    /// Strips the trailing \n (and \r). Returns false on EOF or error.
    /// Blocking; the caller is expected to drive this from a dedicated thread.
    bool read_stdout_line(std::string& out);

    /// Same as read_stdout_line() but for stderr.
    bool read_stderr_line(std::string& out);

    /// Close the child's stdin so it observes EOF.
    void close_stdin();

    /// Force-terminate the child if still running.
    void terminate();

    /// Wait up to \p timeout for the child to exit; returns its exit code, or
    /// std::nullopt if the timeout elapsed first.
    std::optional<int> wait(std::chrono::milliseconds timeout);

    /// True while the child has not yet exited.
    bool is_running();

private:
    Subprocess();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace flowboard
