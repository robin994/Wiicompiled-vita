#pragma once

#include "runtime_config.h"
#include "runtime_log.h"
#include "system_bridge.h"

#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#if defined(MKW_TARGET_VITA)
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#endif

namespace RuntimeNandPath {

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
#if defined(MKW_TARGET_VITA)
    // Always prefer the title's mounted read-only app partition. It follows the
    // actual install location (ux0/uma0/etc.) and is the canonical way for a
    // Vita title to access files packaged in its VPK.
    const auto mounted = RuntimeConfigFile::PathFromUtf8("app0:/wii_bootstrap");
    if (ExistingDirectory(mounted / "shared2" / "wc24")) {
        return mounted;
    }
#endif
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

#if defined(MKW_TARGET_VITA)
inline bool EnsureVitaDirectoryTree(const std::filesystem::path& path) {
    const std::string utf8 = RuntimeConfigFile::PathToUtf8(path);
    if (utf8.empty()) {
        return false;
    }

    // newlib's std::filesystem implementation is sufficient for many Vita
    // paths, but nested device paths have proven unreliable during first-boot
    // seeding. Build every prefix with the native I/O API, ignoring EEXIST-like
    // failures and verifying the final directory afterwards.
    size_t scan = 0;
    for (;;) {
        const size_t slash = utf8.find('/', scan);
        const std::string prefix =
            slash == std::string::npos ? utf8 : utf8.substr(0, slash);
        if (!prefix.empty()) {
            sceIoMkdir(prefix.c_str(), 0777);
        }
        if (slash == std::string::npos) {
            break;
        }
        scan = slash + 1;
    }

    std::error_code verifyError;
    return std::filesystem::is_directory(path, verifyError) && !verifyError;
}

inline bool CopyBootstrapFileVita(const std::filesystem::path& source,
                                  const std::filesystem::path& destination) {
    const std::string sourceUtf8 = RuntimeConfigFile::PathToUtf8(source);
    const std::string destinationUtf8 = RuntimeConfigFile::PathToUtf8(destination);

    const SceUID input = sceIoOpen(sourceUtf8.c_str(), SCE_O_RDONLY, 0);
    if (input < 0) {
        RT_LOGF(RT_TAG_NAND, "bootstrap open failed: %s (0x%08X)\n",
                sourceUtf8.c_str(), static_cast<uint32_t>(input));
        return false;
    }

    const SceUID output = sceIoOpen(destinationUtf8.c_str(),
                                    SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC,
                                    0666);
    if (output < 0) {
        RT_LOGF(RT_TAG_NAND, "bootstrap create failed: %s (0x%08X)\n",
                destinationUtf8.c_str(), static_cast<uint32_t>(output));
        sceIoClose(input);
        return false;
    }

    bool ok = true;
    char buffer[16 * 1024];
    for (;;) {
        const SceSSize bytesRead = sceIoRead(input, buffer, sizeof(buffer));
        if (bytesRead < 0) {
            RT_LOGF(RT_TAG_NAND, "bootstrap read failed: %s (0x%08X)\n",
                    sourceUtf8.c_str(), static_cast<uint32_t>(bytesRead));
            ok = false;
            break;
        }
        if (bytesRead == 0) {
            break;
        }

        SceSSize written = 0;
        while (written < bytesRead) {
            const SceSSize result = sceIoWrite(
                output, buffer + written, static_cast<SceSize>(bytesRead - written));
            if (result <= 0) {
                RT_LOGF(RT_TAG_NAND, "bootstrap write failed: %s (0x%08X)\n",
                        destinationUtf8.c_str(), static_cast<uint32_t>(result));
                ok = false;
                break;
            }
            written += result;
        }
        if (!ok) {
            break;
        }
    }

    sceIoClose(output);
    sceIoClose(input);
    if (!ok) {
        sceIoRemove(destinationUtf8.c_str());
    }
    return ok;
}
#endif

inline bool CopyBootstrapFile(const std::filesystem::path& sourceRoot,
                              const std::filesystem::path& destinationRoot,
                              const std::filesystem::path& relativePath,
                              std::error_code& ec) {
    const auto source = sourceRoot / relativePath;
    const auto destination = destinationRoot / relativePath;
    if (std::filesystem::exists(destination, ec)) {
        return !ec;
    }

    #if defined(MKW_TARGET_VITA)
    if (!EnsureVitaDirectoryTree(destination.parent_path())) {
        RT_LOGF(RT_TAG_NAND, "bootstrap mkdir failed: %s\n",
                RuntimeConfigFile::PathToUtf8(destination.parent_path()).c_str());
        return false;
    }
    return CopyBootstrapFileVita(source, destination);
    #else
    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) {
        return false;
    }
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

inline std::filesystem::path DiscoverNandRootPath() {
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
        FailNandRoot("Configured NAND root is not an existing directory", path);
    }
    return CreateManagedNandRoot();
}

} // namespace RuntimeNandPath
