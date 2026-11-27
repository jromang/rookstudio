#pragma once

#include <chrono>
#include <vector>

namespace fcg {

#ifndef _WIN32

// Ends the process `pid`, a child of this program that has not been reaped yet, at once, with every process it started
// and that they started: the engine behind a wrapper script that does not exec, the engine behind the WinBoard
// adapter. Otherwise they would keep the engine's output open (a read of it would wait for them) and go on running.
// They are all frozen first (SIGSTOP), parents before their children, so that none of them can start another one or
// reap one found, then killed (SIGKILL), children first. Nothing is signalled when `pid` is not such a child (reaped
// already: its pid may be another process's now). False then. Windows needs none of this: an engine's job ends what
// it started.
bool killProcessTree(int pid);

// Until the process `pid`, a child of this program, has ended (not reaped: waitid's WNOWAIT leaves it to whoever reaps
// it), `most` at the longest. True once it has, or when it is not such a child any more (reaped already); false when
// it still runs after `most`.
bool waitChildEnded(int pid, std::chrono::milliseconds most);

// The process `pid` is a child of this program that has ended and was not reaped yet (a zombie), as waitid's WNOWAIT
// tells without reaping it. False while it runs, and once it was reaped: its pid may be another process's by then.
[[nodiscard]] bool childEnded(int pid);

// The processes `pid` started that have not ended (Linux and macOS; none elsewhere).
[[nodiscard]] std::vector<int> childProcesses(int pid);

#endif

}  // namespace fcg
