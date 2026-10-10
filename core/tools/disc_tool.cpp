#include "wiicompiled/disc.hpp"
#include "wiicompiled/disc_index.hpp"
#include <openssl/sha.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;

static void writeAll(const fs::path& path, const void* data, size_t size) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("Cannot create output file: " + path.string());
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!out) throw std::runtime_error("Cannot write output file: " + path.string());
}

static void extractDisc(wiicompiled::Disc& disc, const fs::path& root) {
    fs::create_directories(root);
    std::array<uint8_t, 1024 * 1024> buffer{};
    uint64_t written = 0;
    size_t files = 0;
    for (const auto& entry : disc.entries()) {
        std::string relative = entry.path;
        while (!relative.empty() && (relative.front() == '/' || relative.front() == '\\'))
            relative.erase(relative.begin());
        if (relative.empty()) continue;
        const fs::path destination = root / fs::path(relative);
        if (entry.directory) {
            fs::create_directories(destination);
            continue;
        }
        fs::create_directories(destination.parent_path());
        const fs::path temporary = destination.string() + ".tmp";
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("Cannot create output file: " + destination.string());
        uint64_t offset = 0;
        while (offset < entry.size) {
            const size_t chunk = static_cast<size_t>(std::min<uint64_t>(buffer.size(), entry.size - offset));
            disc.readFile(entry.path, offset, buffer.data(), chunk);
            out.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(chunk));
            if (!out) throw std::runtime_error("Cannot write output file: " + destination.string());
            offset += chunk;
        }
        out.close();
        if (!out) throw std::runtime_error("Cannot finish output file: " + destination.string());
        std::error_code ec;
        fs::rename(temporary, destination, ec);
        if (ec) {
            fs::remove(destination, ec);
            ec.clear();
            fs::rename(temporary, destination, ec);
        }
        if (ec) throw std::runtime_error("Cannot publish output file: " + destination.string());
        written += entry.size;
        ++files;
    }
    const auto index = wiicompiled::encodeDiscIndex(disc);
    writeAll(root / "index.bin", index.data(), index.size());
    // Disc ID, partition groups and region metadata used by unencrypted DI
    // reads. Encrypted partition sectors are deliberately not archived.
    std::vector<uint8_t> rawHeader(0x50000);
    disc.readRaw(0,rawHeader.data(),rawHeader.size());
    writeAll(root / "raw/header.bin",rawHeader.data(),rawHeader.size());
    std::cerr << "extracted_files=" << files << " extracted_bytes=" << written
              << " index_bytes=" << index.size() << '\n';
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: wiicompiled-disc inspect ISO | read ISO /path | hash ISO /path | "
                     "compare ISO /path reference | extract ISO OUTPUT_DIR\n";
        return 2;
    }
    try {
        wiicompiled::Disc disc; disc.open(argv[2]);
        const std::string command=argv[1];
        if (command=="read" && argc==4) {
            const auto bytes=disc.file(argv[3]);
            if (std::fwrite(bytes.data(),1,bytes.size(),stdout)!=bytes.size())
                throw std::runtime_error("Cannot write ISO input stream");
        } else if (command=="inspect" && argc==3) {
            std::cout << "game_id=" << disc.gameId() << " iso_bytes=" << disc.isoSize()
                      << " partition_bytes=" << disc.partitionSize() << " entries=" << disc.entries().size() << '\n';
            for (const auto& e:disc.entries()) if (!e.directory)
                std::cout << e.offset << '\t' << e.size << '\t' << e.path << '\n';
        } else if (command=="extract" && argc==4) {
            extractDisc(disc, fs::path(argv[3]));
        } else if ((command=="hash" && argc==4) || (command=="compare" && argc==5)) {
            const auto* entry=disc.find(argv[3]);
            if (!entry || entry->directory) throw std::runtime_error("File absent from ISO");
            SHA256_CTX hash; SHA256_Init(&hash);
            std::array<uint8_t,128*1024> bytes{}, reference{};
            std::ifstream input;
            if (command=="compare") {
                input.open(argv[4],std::ios::binary);
                if (!input) throw std::runtime_error("Cannot open reference");
                input.seekg(0,std::ios::end);
                if (input.tellg()!=entry->size) throw std::runtime_error("Reference length differs");
                input.seekg(0);
            }
            for (uint64_t offset=0;offset<entry->size;) {
                const size_t length=std::min<uint64_t>(bytes.size(),entry->size-offset);
                disc.readFile(argv[3],offset,bytes.data(),length); SHA256_Update(&hash,bytes.data(),length);
                if (input.is_open()) {
                    input.read(reinterpret_cast<char*>(reference.data()),static_cast<std::streamsize>(length));
                    if (!input || std::memcmp(bytes.data(),reference.data(),length))
                        throw std::runtime_error("ISO bytes differ from reference");
                }
                offset+=length;
            }
            std::array<uint8_t,32> digest{}; SHA256_Final(digest.data(),&hash);
            for (auto b:digest) std::printf("%02x",b);
            std::cout << "  " << entry->path << (command=="compare"?" byte-identical":"") << '\n';
        } else throw std::runtime_error("Invalid arguments");
        const auto s=disc.stats();
        std::cerr << "raw_bytes=" << s.rawBytes << " requested_bytes=" << s.requestedBytes
                  << " cache_hits=" << s.cacheHits << " cache_misses=" << s.cacheMisses << '\n';
        return 0;
    } catch (const std::exception& e) { std::cerr << "disc: " << e.what() << '\n'; return 1; }
}
