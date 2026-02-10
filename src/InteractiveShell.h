#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

#if defined(__linux__) || defined(__APPLE__)
#include <pty.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#endif

namespace shell {

using ShellCallback = std::function<void(const std::string& output)>;

class InteractiveShell {
public:
    InteractiveShell();
    ~InteractiveShell();

    // Exactly the same signatures as Windows version
    void StartShell(ShellCallback callback);
    void StopShell();
    void WriteToShell(const std::string& command);
    bool IsShellRunning() const;

    // Optional: notify about window size change from implant main thread / task dispatcher
    void NotifyResize(int cols, int rows);

private:
    void ReaderThreadFunc();
    void SignalForwardThreadFunc();

    static void SigwinchHandler(int sig);
    static void ForwardSignalToChild(int sig);

    ShellCallback                  m_callback           = nullptr;
    std::atomic<bool>              m_running            {false};
    std::atomic<pid_t>             m_child_pid          {-1};
    int                            m_master_fd          = -1;
    std::thread                    m_reader_thread;
    std::thread                    m_signal_thread;      // optional - for forwarding SIGINT etc.

    // Current terminal size (updated via SIGWINCH or NotifyResize)
    std::atomic<int>               m_cols               {80};
    std::atomic<int>               m_rows               {24};

    // Used to avoid multiple resize events piling up
    std::atomic<bool>              m_resize_pending     {false};

    static InteractiveShell*       s_active_instance;   // singleton-ish for signal handler
};

} // namespace shell
