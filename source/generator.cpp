/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native compute integration adapted from Cemu/NetherSX2's Deko3D LSFG
 * implementation. Copyright 2026 Dolphin Emulator Project and
 * PalindromicBreadLoaf (palindromicbreadloaf@tuta.com).
 */
#include <sfg/generator.hpp>
#include <deko3d.hpp>
#include <lsfg/backend/binding.hpp>
#include <lsfg/backend/cache_load.hpp>
#include <lsfg/backend/schedule.hpp>
#include <lsfg/common/dksh.hpp>
#include <lsfg/common/cache_store.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <fstream>
#include <vector>

namespace sfg {
namespace backend = lsfg::backend;
namespace graph = lsfg::graph;
namespace {
constexpr uint64_t kBudget = 192ull * 1024 * 1024;
constexpr unsigned kScratch = 65536;
constexpr unsigned kCpuGpu = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
DkImageFormat format(graph::Format f) {
  switch (f) {
    case graph::Format::rgba8: return DkImageFormat_RGBA8_Unorm;
    case graph::Format::r8: return DkImageFormat_R8_Unorm;
    case graph::Format::rgba16f: return DkImageFormat_RGBA16_Float;
  }
  return DkImageFormat_None;
}
bool status(lsfg::ErrorCode code, const char *operation, std::string &error) {
  if (lsfg::succeeded(code)) return true;
  error = std::string("LSFG Deko3D: ") + operation + " (";
  error += lsfg::error_name(code); error += ")";
  return false;
}
}

struct Generator::State {
  DkDevice device{};
  graph::Config config;
  lsfg::cache::Loaded cache;
  backend::Schedule schedule;
  backend::Plan plan;
  backend::DescriptorLayout descriptors;
  dk::UniqueMemBlock code_memory, image_memory, descriptor_memory, uniform_memory;
  std::vector<DkShader> shaders;
  std::vector<dk::Image> images;
  std::vector<uint8_t> hazards;
  unsigned phase{}, warmup{}, generated_index{UINT32_MAX};

  dk::UniqueMemBlock allocate(uint64_t size, unsigned flags) {
    size = (size + DK_MEMBLOCK_ALIGNMENT - 1) & ~(uint64_t(DK_MEMBLOCK_ALIGNMENT) - 1);
    if (!size || size > backend::max_memory_block_bytes) return {};
    return dk::MemBlockMaker{device, uint32_t(size)}.setFlags(flags).create();
  }
  bool prepare(const char *directory, std::string &error) {
    if (!status(lsfg::cache::read(directory, cache), "cache validation failed", error)) return false;
    config = lsfg::cache::configuration(cache.header);
    if (!config.performance || config.hdr || config.generated_frames != 1 ||
        config.flow_numerator != 1 || config.flow_denominator != 4 ||
        cache.header.shader_precision != uint32_t(lsfg::cache::Precision::high)) {
      error = "LSFG: expected Performance / high precision / 2x / quarter-flow cache";
      return false;
    }
    return status(backend::schedule(cache.graph, schedule), "schedule failed", error);
  }
  bool load_shaders(std::string &error) {
    backend::Arena arena;
    std::vector<uint32_t> offsets;
    for (const auto &pass : cache.passes) {
      lsfg::dksh::ComputeProgram program;
      if (!status(lsfg::dksh::validate(pass.dksh, program), "invalid compute shader", error)) return false;
      lsfg::dksh::FileHeader header;
      std::memcpy(&header, pass.dksh.data(), sizeof(header));
      offsets.push_back(arena.place(header.code_size + DK_SHADER_CODE_UNUSABLE_SIZE, DK_SHADER_CODE_ALIGNMENT));
    }
    if (arena.overflowed()) { error = "LSFG Deko3D: shader code exceeds the address range"; return false; }
    code_memory = allocate(arena.block_size(), kCpuGpu | DkMemBlockFlags_Code);
    if (!code_memory) { error = "LSFG Deko3D: shader allocation failed"; return false; }
    shaders.resize(cache.passes.size());
    for (unsigned i = 0; i < shaders.size(); i++) {
      const auto &pass = cache.passes[i];
      lsfg::dksh::FileHeader header;
      std::memcpy(&header, pass.dksh.data(), sizeof(header));
      std::memcpy(static_cast<uint8_t *>(code_memory.getCpuAddr()) + offsets[i],
          pass.dksh.data() + header.control_size, header.code_size);
      DkShaderMaker maker; dkShaderMakerDefaults(&maker, code_memory, offsets[i]);
      maker.control = pass.dksh.data();
      dkShaderInitialize(&shaders[i], &maker);
      if (!dkShaderIsValid(&shaders[i]) || dkShaderGetStage(&shaders[i]) != DkStage_Compute) {
        error = "LSFG Deko3D: compute shader initialization failed"; return false;
      }
    }
    return true;
  }
  bool allocate_images(unsigned width, unsigned height, std::string &error) {
    const backend::Request request{.config = config, .precision = lsfg::cache::Precision::high,
        .output = {width, height}, .memory_budget_bytes = kBudget};
    backend::Rejection why;
    if (!backend::accept(cache, request, plan, why)) {
      error = "LSFG Deko3D: "; error += why.reason;
      error += ". Try Quarter flow resolution / Performance mode."; return false;
    }
    if (plan.max_scratch_bytes_per_warp > kScratch) {
      error = "LSFG Deko3D: compute scratch exceeds the queue capacity"; return false;
    }
    if (!status(backend::describe(cache.graph, descriptors), "descriptor layout failed", error)) return false;
    std::vector<dk::ImageLayout> layouts(plan.images.size());
    std::vector<uint32_t> offsets;
    backend::Arena arena;
    for (unsigned i = 0; i < plan.images.size(); i++) {
      const auto &p = plan.images[i];
      unsigned flags = DkImageFlags_UsageLoadStore;
      if (p.role == graph::ImageRole::history) flags |= DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine;
      if (p.role == graph::ImageRole::generated) {
        flags |= DkImageFlags_Usage2DEngine;
        if (generated_index == UINT32_MAX) generated_index = i;
      }
      dk::ImageLayoutMaker{device}.setFormat(format(p.format)).setFlags(flags)
          .setDimensions(p.extent.width, p.extent.height).initialize(layouts[i]);
      offsets.push_back(arena.place(layouts[i].getSize(), layouts[i].getAlignment()));
    }
    if (arena.overflowed() || arena.block_size() > kBudget || generated_index == UINT32_MAX) {
      error = "LSFG Deko3D: image pool exceeds the 192 MiB budget"; return false;
    }
    image_memory = allocate(arena.block_size(), DkMemBlockFlags_GpuCached |
        DkMemBlockFlags_Image | DkMemBlockFlags_ZeroFillInit);
    descriptor_memory = allocate(uint64_t(descriptors.image_descriptors + backend::sampler_descriptor_count) * sizeof(DkImageDescriptor), kCpuGpu);
    if (plan.uniform_buffers) uniform_memory = allocate(uint64_t(plan.uniform_buffers) * backend::uniform_buffer_stride, kCpuGpu);
    if (!image_memory || !descriptor_memory || (plan.uniform_buffers && !uniform_memory)) {
      error = "LSFG Deko3D: image / descriptor allocation failed"; return false;
    }
    images.resize(plan.images.size());
    auto *table = static_cast<DkImageDescriptor *>(descriptor_memory.getCpuAddr());
    for (unsigned i = 0; i < images.size(); i++) {
      images[i].initialize(layouts[i], image_memory, offsets[i]);
      const auto &d = descriptors.images[i];
      DkImageView view; dkImageViewDefaults(&view, &images[i]);
      if (d.sampled != backend::no_descriptor) dkImageDescriptorInitialize(&table[d.sampled], &view, false, false);
      if (d.storage != backend::no_descriptor) dkImageDescriptorInitialize(&table[d.storage], &view, true, false);
    }
    auto *samplers = reinterpret_cast<DkSamplerDescriptor *>(table + descriptors.image_descriptors);
    for (unsigned i = 0; i < backend::sampler_descriptor_count; i++) {
      DkSampler sampler; dkSamplerDefaults(&sampler);
      sampler.minFilter = sampler.magFilter = DkFilter_Linear;
      sampler.mipFilter = DkMipFilter_Linear;
      const bool edge = i == unsigned(graph::Sampler::edge) || i == backend::introduced_sampler;
      for (auto &wrap : sampler.wrapMode) wrap = edge ? DkWrapMode_ClampToEdge : DkWrapMode_ClampToBorder;
      if (i == unsigned(graph::Sampler::border_white)) for (auto &c : sampler.borderColor) c.value_f = 1.f;
      dkSamplerDescriptorInitialize(&samplers[i], &sampler);
    }
    for (unsigned i = 0; i < plan.uniform_buffers; i++) {
      const auto constants = graph::constant_buffer(i, config);
      std::memcpy(static_cast<uint8_t *>(uniform_memory.getCpuAddr()) + i * backend::uniform_buffer_stride,
          &constants, sizeof(constants));
    }
    hazards.assign(images.size(), 0);
    std::printf("LSFG Deko3D: %ux%u, %u passes, %u images, %u MiB image pool\n",
        width, height, unsigned(shaders.size()), unsigned(images.size()), arena.block_size() / (1024 * 1024));
    return true;
  }
  bool dispatch(DkCmdBuf cmd, unsigned index, std::string &error) {
    backend::DispatchBinding b;
    if (!status(backend::bind(cache, plan, descriptors, index, phase, b), "dispatch binding failed", error)) return false;
    bool barrier = false; unsigned invalidate = 0;
    for (unsigned i = 0; i < b.texture_count; i++) if (hazards[b.textures[i].image] & 2) {
      barrier = true; invalidate |= DkInvalidateFlags_Image;
    }
    for (unsigned i = 0; i < b.storage_count; i++) if (hazards[b.storages[i].image]) barrier = true;
    if (barrier) {
      dkCmdBufBarrier(cmd, DkBarrier_Primitives, invalidate);
      std::fill(hazards.begin(), hazards.end(), 0);
    }
    std::array<DkResHandle, backend::max_texture_slots> textures{};
    std::array<DkResHandle, backend::max_storage_slots> storages{};
    unsigned nt = 0, ns = 0;
    for (unsigned i = 0; i < b.texture_count; i++) {
      const auto &t = b.textures[i];
      textures[t.slot] = dkMakeTextureHandle(t.descriptor, t.sampler);
      nt = std::max(nt, t.slot + 1); hazards[t.image] |= 1;
    }
    for (unsigned i = 0; i < b.storage_count; i++) {
      const auto &t = b.storages[i];
      storages[t.slot] = dkMakeImageHandle(t.descriptor);
      ns = std::max(ns, t.slot + 1); hazards[t.image] |= 2;
    }
    const DkShader *shader = &shaders[b.pass];
    dkCmdBufBindShaders(cmd, DkStageFlag_Compute, &shader, 1);
    if (nt) dkCmdBufBindTextures(cmd, DkStage_Compute, 0, textures.data(), nt);
    if (ns) dkCmdBufBindImages(cmd, DkStage_Compute, 0, storages.data(), ns);
    if (b.uniform_slot != backend::no_slot) {
      const DkBufExtents uniform{uniform_memory.getGpuAddr() + b.uniform_buffer * backend::uniform_buffer_stride,
                                backend::uniform_buffer_stride};
      dkCmdBufBindUniformBuffers(cmd, DkStage_Compute, b.uniform_slot, &uniform, 1);
    }
    dkCmdBufDispatchCompute(cmd, b.groups_x, b.groups_y, 1);
    return true;
  }
};

Generator::Generator(std::unique_ptr<State> state) : state_(std::move(state)) {}
Generator::~Generator() = default;
std::unique_ptr<Generator> Generator::create(DkDevice device,
    unsigned width, unsigned height, const char *directory, std::string &error) {
  try {
    if (!device || !width || !height || !directory) {
      error = "LSFG: invalid device, dimensions or cache directory"; return {};
    }
    auto s = std::make_unique<State>(); s->device = device;
    if (!s->prepare(directory, error) || !s->load_shaders(error) ||
        !s->allocate_images(width, height, error)) return {};
    return std::unique_ptr<Generator>(new Generator(std::move(s)));
  } catch (const std::exception &e) { error = std::string("LSFG Deko3D: ") + e.what(); return {}; }
}
const DkImage *Generator::input() const {
  return &state_->images[state_->phase % graph::history_image_count];
}
void Generator::reset() { state_->warmup = 0; }
bool Generator::record(DkCmdBuf cmd, const DkImage *&generated,
    const DkImage *&original, std::string &error, float alpha) {
  if (!std::isfinite(alpha) || alpha <= 0.f || alpha >= 1.f) {
    generated = nullptr; original = nullptr;
    error = "LSFG: interpolation alpha must be strictly between 0 and 1";
    return false;
  }
  auto &s = *state_; generated = nullptr; original = input();
  /* The game is rendered straight into this phase's history image. Order its
   * color writes before sampling, including descriptor and uniform visibility. */
  dkCmdBufBarrier(cmd, DkBarrier_Full,
      DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors | DkInvalidateFlags_Shader);
  auto constants = graph::constant_buffer(1, s.config);
  constants.timestamp = alpha;
  dkCmdBufPushConstants(cmd, s.uniform_memory.getGpuAddr() + backend::uniform_buffer_stride,
      backend::uniform_buffer_stride, 0, sizeof(constants), &constants);
  dkCmdBufBarrier(cmd, DkBarrier_Full, DkInvalidateFlags_Shader);
  dkCmdBufBindImageDescriptorSet(cmd, s.descriptor_memory.getGpuAddr(), s.descriptors.image_descriptors);
  dkCmdBufBindSamplerDescriptorSet(cmd, s.descriptor_memory.getGpuAddr() +
      s.descriptors.image_descriptors * sizeof(DkImageDescriptor), backend::sampler_descriptor_count);
  std::fill(s.hazards.begin(), s.hazards.end(), 0);
  for (unsigned i = 0; i < s.cache.graph.dispatches.size(); i++)
    if (!s.dispatch(cmd, i, error)) return false;
  dkCmdBufBarrier(cmd, DkBarrier_Full, DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors);
  if (s.warmup < s.schedule.warmup_frames) s.warmup++;
  else generated = &s.images[s.generated_index];
  s.phase = (s.phase + 1) % s.schedule.cycle;
  return true;
}
} // namespace sfg
