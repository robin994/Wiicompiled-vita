#pragma once

#include "runtime_config.h"
#include "nand_settings.h"
#include "runtime_log.h"
#include "system_bridge.h"

#include <cstdlib>
#include <cstdio>
#include <array>
#include <atomic>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace RuntimeNandPath {

#if defined(MKW_TARGET_VITA)
inline std::filesystem::path g_vitaNandRoot;
inline std::atomic<bool> g_vitaNandRootReady{false};
inline std::atomic_flag g_vitaNandRootLock = ATOMIC_FLAG_INIT;
#endif

inline std::optional<std::filesystem::path> ExistingDirectory(const std::filesystem::path& path) {
    std::error_code ec;
    if (!path.empty() && std::filesystem::is_directory(path, ec) && !ec) {
        return path;
    }
    return std::nullopt;
}

[[noreturn]] inline void FailNandRoot(const char* message, const std::filesystem::path& path = {}) {
    if (path.empty()) {
        RT_LOGF(RT_TAG_NAND, "ERROR: %s\n", message);
    } else {
        RT_LOGF(RT_TAG_NAND, "ERROR: %s: %s\n", message,
                RuntimeConfigFile::PathToUtf8(path).c_str());
    }
    RT_LOGF(RT_TAG_NAND, "Set [paths] nand_root in Config.toml.\n");
    std::string details = message ? message : "The configured NAND could not be initialized.";
    if (!path.empty()) {
        details += "\n\nPath: ";
        details += RuntimeConfigFile::PathToUtf8(path);
    }
    details += "\n\nSet [paths] nand_root in Config.toml and try again.";
    // Same fatal idiom as the DVD and OS paths: crash artifacts first so the run
    // folder always has them, then a non-zero exit code, the popup, and the
    // "already reported" latch so the atexit handler does not stack a second
    // generic report on top of this one.
    RuntimeCrash::WriteCrashArtifacts("nand_root", details);
    SetRuntimeExitCode(EXIT_FAILURE);
    ShowRuntimeFatalPopup("NAND initialization failed", details);
    MarkFatalErrorReported();
    std::exit(EXIT_FAILURE);
}

inline std::filesystem::path ResolveConfiguredPath(const std::string& value) {
    return RuntimeConfigFile::ResolveRelativeToConfig(value);
}

inline std::filesystem::path ManagedNandRootPath() {
    return RuntimeConfigFile::ApplicationDataDirectory() / "NAND";
}

inline std::optional<std::filesystem::path> BootstrapPayloadPath() {
    if (auto executableDirectory = RuntimeConfigFile::ExecutableDirectory()) {
        const auto adjacent = *executableDirectory / "wii_bootstrap";
        if (ExistingDirectory(adjacent / "shared2" / "wc24")) {
            return adjacent;
        }
    }

    // This makes developer-tree launches work without changing their release layout.
    for (auto base = std::filesystem::current_path(); !base.empty();) {
        const auto candidate = base / "runtime" / "assets" / "wii";
        if (ExistingDirectory(candidate / "shared2" / "wc24")) {
            return candidate;
        }
        const auto parent = base.parent_path();
        if (parent == base) {
            break;
        }
        base = parent;
    }
    return std::nullopt;
}

inline bool CopyBootstrapFile(const std::filesystem::path& sourceRoot,
                              const std::filesystem::path& destinationRoot,
                              const std::filesystem::path& relativePath,
                              std::error_code& ec) {
    const auto source = sourceRoot / relativePath;
    const auto destination = destinationRoot / relativePath;
    if (std::filesystem::exists(destination, ec)) {
        return !ec;
    }

    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) {
        return false;
    }
#if defined(MKW_TARGET_VITA)
    // Vita's std::filesystem::copy_file backend does not reliably copy across
    // device prefixes (app0: -> ux0:). Use plain stdio so first-run bootstrap
    // files can be seeded from the read-only application mount.
    std::FILE* input = std::fopen(source.c_str(), "rb");
    if (!input) {
        ec = std::error_code(errno, std::generic_category());
        return false;
    }
    std::FILE* output = std::fopen(destination.c_str(), "wb");
    if (!output) {
        const int savedErrno = errno;
        std::fclose(input);
        ec = std::error_code(savedErrno, std::generic_category());
        return false;
    }

    bool ok = true;
    std::array<unsigned char, 4096> buffer{};
    for (;;) {
        const size_t read = std::fread(buffer.data(), 1, buffer.size(), input);
        if (read != 0 && std::fwrite(buffer.data(), 1, read, output) != read) {
            ok = false;
            break;
        }
        if (read < buffer.size()) {
            if (std::ferror(input)) {
                ok = false;
            }
            break;
        }
    }
    if (std::fclose(output) != 0) {
        ok = false;
    }
    std::fclose(input);
    if (!ok) {
        ec = std::error_code(errno ? errno : EIO, std::generic_category());
        return false;
    }
    ec.clear();
    return true;
#else
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none, ec);
    return !ec;
#endif
}

// Create these WC24 files only for a new profile; never overwrite user data.
constexpr std::string_view kBootstrapFiles[] = {
    "shared2/wc24/misc.bin",
    "shared2/wc24/nwc24dl.bin",
    "shared2/wc24/nwc24fl.bin",
    "shared2/wc24/nwc24fls.bin",
    "shared2/wc24/nwc24msg.cbk",
    "shared2/wc24/nwc24msg.cfg",
    "shared2/wc24/mbox/Readme.txt",
    "shared2/wc24/mbox/wc24recv.ctl",
    "shared2/wc24/mbox/wc24recv.mbx",
    "shared2/wc24/mbox/wc24send.ctl",
    "shared2/wc24/mbox/wc24send.mbx",
};

// Add first-run WC24 files only when the NAND has none yet.
inline bool SeedMissingBootstrapFiles(const std::filesystem::path& root) {
    const auto payload = BootstrapPayloadPath();
    if (!payload) {
        return false;
    }
    std::error_code ec;
    for (const std::string_view file : kBootstrapFiles) {
        const std::filesystem::path relativePath{std::string(file)};
        ec.clear();
        if (!CopyBootstrapFile(*payload, root, relativePath, ec)) {
            RT_LOG(RT_TAG_NAND) << "could not create "
                                << RuntimeConfigFile::PathToUtf8(root / relativePath)
                                << std::endl;
            return false;
        }
    }
    return true;
}

inline std::filesystem::path CreateManagedNandRoot() {
    const std::filesystem::path root = ManagedNandRootPath();
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (ec || !std::filesystem::is_directory(root, ec)) {
        FailNandRoot("Unable to create managed NAND root", root);
    }

    if (!SeedMissingBootstrapFiles(root)) {
        FailNandRoot("Unable to initialize managed NAND", root);
    }

    const auto marker = root / ".mkw_recompiled_managed_nand";
    ec.clear();
    if (!std::filesystem::exists(marker, ec)) {
        std::ofstream markerFile(marker, std::ios::trunc);
        if (!markerFile) {
            FailNandRoot("Managed NAND root is not writable", root);
        }
        markerFile << "version=1\n";
        markerFile.close();
        if (!markerFile) {
            FailNandRoot("Unable to finish managed NAND initialization", root);
        }
    }

    RT_LOG(RT_TAG_NAND) << "using managed NAND root: " << RuntimeConfigFile::PathToUtf8(root)
                        << std::endl;
    return root;
}

inline std::filesystem::path ResolveNandRootPath() {
    const std::string configPath = RuntimeConfigFile::NandRoot();
    if (!configPath.empty()) {
        const auto path = ResolveConfiguredPath(configPath);
        if (auto existing = ExistingDirectory(path)) {
            // Seed only a new configured NAND so existing frontend data stays unchanged.
            if (!SeedMissingBootstrapFiles(*existing)) {
                RT_LOG(RT_TAG_NAND) << "first-run WC24 seeding failed for the configured NAND root" << std::endl;
            }
            return *existing;
        }
#if defined(MKW_TARGET_VITA)
        // Vita's generated Config.toml intentionally points at the app's writable
        // data directory. Treat a missing configured root as a first-run profile
        // instead of requiring the user to create the directory by hand.
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
        if (!ec) {
            ec.clear();
            if (std::filesystem::is_directory(path, ec) && !ec) {
                if (!SeedMissingBootstrapFiles(path)) {
                    FailNandRoot("Unable to initialize configured NAND", path);
                }
                RT_LOGF(RT_TAG_NAND, "created Vita NAND root: %s\n",
                        RuntimeConfigFile::PathToUtf8(path).c_str());
                return path;
            }
        }
#endif
        FailNandRoot("Configured NAND root is not an existing directory", path);
    }
    return CreateManagedNandRoot();
}

inline std::filesystem::path DiscoverNandRootPath() {
#if defined(MKW_TARGET_VITA)
    if (!g_vitaNandRootReady.load(std::memory_order_acquire)) {
        while (g_vitaNandRootLock.test_and_set(std::memory_order_acquire)) {
            std::atomic_signal_fence(std::memory_order_seq_cst);
        }
        if (!g_vitaNandRootReady.load(std::memory_order_relaxed)) {
            RT_LOGF(RT_TAG_NAND, "initializing Vita NAND root\n");
            const auto resolved = ResolveNandRootPath();
            std::string error;
            if (!RuntimeNandSettings::Ensure(resolved, error)) {
                g_vitaNandRootLock.clear(std::memory_order_release);
                FailNandRoot(error.c_str(), RuntimeNandSettings::FilePath(resolved));
            }
            g_vitaNandRoot = resolved;
            g_vitaNandRootReady.store(true, std::memory_order_release);
            RT_LOGF(RT_TAG_NAND, "Vita NAND root ready: %s\n",
                    RuntimeConfigFile::PathToUtf8(g_vitaNandRoot).c_str());
        }
        g_vitaNandRootLock.clear(std::memory_order_release);
    }
    return g_vitaNandRoot;
#else
    static const auto root = [] {
        const auto resolved = ResolveNandRootPath();
        std::string error;
        if (!RuntimeNandSettings::Ensure(resolved, error)) {
            FailNandRoot(error.c_str(), RuntimeNandSettings::FilePath(resolved));
        }
        return resolved;
    }();
    return root;
#endif
}

} // namespace RuntimeNandPath
