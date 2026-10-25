// SPDX-License-Identifier: GPL-3.0-or-later
#include <lsfg/common/prepare.hpp>
#include <lsfg/backend/cache_load.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

static void require(lsfg::ErrorCode code, const char* operation) {
    if (!lsfg::succeeded(code))
        throw std::runtime_error(std::string(operation) + ": " + std::string(lsfg::error_name(code)));
}

int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("usage: lsfg_prepare Lossless.dll cache-root");
        std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
        auto size = file ? file.tellg() : std::streampos(-1);
        if (size <= 0 || size > 128 * 1024 * 1024) throw std::runtime_error("DLL missing or invalid size");
        std::vector<uint8_t> dll(static_cast<size_t>(size));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(dll.data()), dll.size())) throw std::runtime_error("DLL read failed");
        lsfg::prepare::Options options;
        options.graph.performance = true;
        options.graph.flow_denominator = 4;
        options.graph.generated_frames = 1;
        options.keep_glsl = true;
        lsfg::prepare::Result result;
        require(lsfg::prepare::run(dll, options, result, [](const auto& module) {
            std::cout << module.name << ": " << module.dksh_bytes << " bytes, "
                      << module.registers << " registers\n" << std::flush;
        }), "LSFG preparation");
        const auto directory = lsfg::prepare::directory_for(argv[2], result);
        require(lsfg::cache::write(directory, result.contents), "Write cache");
        lsfg::cache::Loaded loaded;
        require(lsfg::cache::read(directory, loaded), "Verify cache");
        if (!lsfg::cache::same_modules(loaded, result.contents)) throw std::runtime_error("Cache round-trip mismatch");
        std::filesystem::create_directories(std::filesystem::path(directory) / "glsl");
        for (size_t i=0; i<result.reports.size(); ++i) {
            char filename[32]; std::snprintf(filename,sizeof(filename),"%02u.comp",unsigned(i));
            std::ofstream glsl(std::filesystem::path(directory) / "glsl" / filename);
            glsl << result.reports[i].glsl;
            if(!glsl) throw std::runtime_error("Could not export desktop GLSL");
        }
        unsigned accepted = 0;
        for (const auto extent : {lsfg::graph::Extent{640,360}, {960,540}, {1280,720}}) {
            lsfg::backend::Request request{.config = options.graph, .output = extent,
                .memory_budget_bytes = 192ull * 1024 * 1024};
            lsfg::backend::Plan plan; lsfg::backend::Rejection why;
            if (!lsfg::backend::accept(loaded, request, plan, why)) {
                std::cout << extent.width << 'x' << extent.height << ": unsupported (" << why.reason << ")\n";
                continue;
            }
            ++accepted;
            std::cout << extent.width << 'x' << extent.height << ": " << plan.owned_image_bytes
                      << " image bytes; scratch " << plan.max_scratch_bytes_per_warp << '\n';
        }
        if (!accepted) throw std::runtime_error("No supported demo resolutions");
        std::filesystem::create_directories(argv[2]);
        std::ofstream selection(std::filesystem::path(argv[2]) / "selected.txt");
        selection << std::filesystem::path(directory).filename().string() << '\n';
        if (!selection) throw std::runtime_error("Could not write cache selection");
        std::cout << "CACHE=" << directory << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
