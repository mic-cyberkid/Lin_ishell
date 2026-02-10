// src/main.cpp
#include "InteractiveShell.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

std::atomic<bool> g_running{true};

void sigint_handler(int) {
    g_running = false;
}

int main() {
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);

    shell::InteractiveShell shell;

    shell.StartShell([](const std::string& output) {
        if (output.starts_with("ISHELL_OUTPUT:")) {
            std::cout << output.substr(14);
            std::cout.flush();
        } else {
            std::cout << output << std::endl;
        }
    });

    std::cout << "[i] PTY shell started. Type commands (empty line + Enter = exit)\n";
    std::cout << "    Ctrl+C or 'exit' to quit\n\n";

    std::string line;
    while (g_running && shell.IsShellRunning()) {
        std::cout << "shell> " << std::flush;
        if (!std::getline(std::cin, line)) {
            break;
        }

        if (line.empty()) {
            break;
        }

        if (line == "exit" || line == "quit") {
            break;
        }

        if (line == "resize 120 40") {  // example manual resize test
            shell.NotifyResize(120, 40);
            std::cout << "[i] Sent resize 120×40\n";
            continue;
        }

        shell.WriteToShell(line);
    }

    std::cout << "\n[i] Stopping shell...\n";
    shell.StopShell();

    std::cout << "[i] Done.\n";
    return 0;
}
