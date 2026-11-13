// Entry point: command-line flags and the REPL loop only.
// All database logic lives in the minikv_core library.

#include <unistd.h>

#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "minikv/clock.hpp"
#include "minikv/commands.hpp"
#include "minikv/persistence.hpp"
#include "minikv/store.hpp"

namespace {

constexpr int kExitOk = 0;
constexpr int kExitError = 1;
constexpr int kExitUsage = 2;

void print_usage(std::ostream &out) {
    out << "Usage: minikv [--file <path>] [--no-save] [--help]\n"
           "\n"
           "  --file <path>  Snapshot file to load from and save to (default: minikv.db)\n"
           "  --no-save      Pure in-memory mode: do not load or save any file\n"
           "  --help         Print this message and exit\n";
}

struct Options {
    std::string file = "minikv.db";
    bool no_save = false;
};

} // namespace

int main(int argc, char *argv[]) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--help") {
            print_usage(std::cout);
            return kExitOk;
        }
        if (arg == "--no-save") {
            options.no_save = true;
        } else if (arg == "--file" && i + 1 < argc) {
            options.file = argv[++i];
        } else {
            // Unknown flag, or --file with no path after it.
            print_usage(std::cerr);
            return kExitUsage;
        }
    }

    minikv::SystemClock clock;
    minikv::Store store(clock);

    std::optional<minikv::SnapshotFile> snapshot;
    if (!options.no_save) {
        snapshot.emplace(options.file);
        try {
            snapshot->load(store, std::cerr);
        } catch (const minikv::PersistenceError &error) {
            std::cerr << "error: " << error.what() << '\n';
            return kExitError;
        }
    }

    minikv::CommandProcessor processor(store, std::move(snapshot));

    // Prompt and banner only for a human at a terminal; piped input gets clean,
    // diff-able output.
    const bool interactive = isatty(STDIN_FILENO) != 0;
    if (interactive) {
        std::cout << "MiniKV 1.0 \xE2\x80\x94 type HELP for commands, EXIT to quit\n";
    }

    std::string line;
    while (true) {
        if (interactive) {
            std::cout << "minikv> " << std::flush;
        }
        if (!std::getline(std::cin, line)) {
            break; // EOF (Ctrl-D or end of piped input)
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back(); // tolerate Windows line endings in piped files
        }
        std::cout << processor.execute(line) << std::flush;
        if (processor.exit_requested()) {
            return kExitOk;
        }
    }

    if (interactive) {
        std::cout << '\n'; // so the shell prompt starts on a fresh line after Ctrl-D
    }
    if (auto save_error = processor.save_on_shutdown()) {
        std::cerr << *save_error;
        return kExitError;
    }
    return kExitOk;
}
