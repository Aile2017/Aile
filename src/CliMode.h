#pragma once
#include <windows.h>
#include <vector>
#include <string>

// CLI entry point for non-UI execution.
// If the first argument is a known subcommand (x/e/t/l), enters CLI mode.
// Exit codes:
//   0   = success
//   2   = fatal error (HRESULT failure, etc.)
//   7   = argument error / usage mistake
//   255 = user cancelled (Ctrl+C / Ctrl+Break)
namespace CliMode {

// Returns true if the given command line should enter CLI mode.
// True if the first non-flag argument is "x", "e", "t", or "l".
bool IsCliCommand(int argc, wchar_t** argv);

// Run CLI mode. Outputs to stdout/stderr via AttachConsole + freopen.
// Return value is used directly as the process exit code.
int Run(int argc, wchar_t** argv);

} // namespace CliMode
