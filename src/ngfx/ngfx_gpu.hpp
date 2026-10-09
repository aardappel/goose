// The ngfx layer's GPU half, shared by ngfx_device.cpp, ngfx_resources.cpp and
// ngfx_draw.cpp: NoGraphicsAPI and NoGraphicsAPIUtility, used rather than
// ported, so that their synchronization is the module's by construction
// (docs/design/ngfx.md). Each barrier the layer records itself names its
// source where it is recorded: one of NoGraphicsAPI's examples or tests, or
// the layer's synchronization rules.
//
// One device, one queue (zero), one timeline. Frames are double-buffered as
// the examples are: a command pool and a slice of mapped frame memory per
// frame in flight, reused once the timeline says the frame before last has
// finished. Uploads go through an UploadQueue on the same queue, flushed
// before every submission of the frame's commands, so a frame's draws see
// every upload made before it is submitted. Releases wait in a DeleteQueue
// for the submission that last used them.

#pragma once

#include <NoGraphicsAPI/NoGraphicsAPI.hpp>
#include <NoGraphicsAPIUtility/bump_allocator.hpp>
#include <NoGraphicsAPIUtility/delete_queue.hpp>
#include <NoGraphicsAPIUtility/heap_allocator.hpp>
#include <NoGraphicsAPIUtility/texture_allocator.hpp>
#include <NoGraphicsAPIUtility/texture_upload.hpp>
#include <NoGraphicsAPIUtility/upload_queue.hpp>

#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "ngfx_blob.h"
#include "ngfx_internal.h"

namespace ngfx {

using namespace gpu;

inline bool has(TextureUsage usage, TextureUsage flag) { return (uint32(usage) & uint32(flag)) != 0; }

// --- handles ----------------------------------------------------------------
// gfx's handle tables (src/gfx/gfx_device.c): a handle is (generation << 20) |
// index, so a released or stale handle is a misuse, not a crash.

constexpr uint32 index_bits = 20;
constexpr uint32 index_mask = (1u << index_bits) - 1;

template<typename T>
struct Table
{
    const char* what;
    std::vector<T> items;
    std::vector<uint16> gens;
    std::vector<uint8> live;
    std::vector<uint32> freelist;

    explicit Table(const char* name) : what(name) {}

    T* add(uint32& id)
    {
        uint32 i;
        if (!freelist.empty())
        {
            i = freelist.back();
            freelist.pop_back();
        }
        else
        {
            if (items.size() >= index_mask)
            {
                ngfx_fail("too many %ss", what);
                return nullptr;
            }
            i = uint32(items.size());
            items.emplace_back();
            gens.push_back(0);
            live.push_back(0);
        }
        gens[i] = uint16(gens[i] % 4095 + 1);
        live[i] = 1;
        items[i] = T{};
        id = (uint32(gens[i]) << index_bits) | i;
        return &items[i];
    }

    T* find(uint32 id)
    {
        const uint32 i = id & index_mask;
        if (!id || i >= items.size() || !live[i] || gens[i] != id >> index_bits) return nullptr;
        return &items[i];
    }

    T* get(uint32 id)
    {
        T* item = find(id);
        if (!item)
        {
            if (!id) ngfx_misuse("a null %s handle", what);
            else ngfx_misuse("a %s handle that was released, or never created", what);
        }
        return item;
    }

    void remove(uint32 id)
    {
        if (!find(id)) return;
        const uint32 i = id & index_mask;
        live[i] = 0;
        freelist.push_back(i);
    }

    template<typename F>
    void each(F f)
    {
        for (size_t i = 0; i < items.size(); ++i)
            if (live[i]) f(items[i]);
    }

    void clear()
    {
        items.clear();
        gens.clear();
        live.clear();
        freelist.clear();
    }
};

// --- resources ----------------------------------------------------------------

struct BufferSlot
{
    GpuRange range{};               // the program's bytes: address and size
    HeapAllocation<byte> allocation{};
    int32 chunk = -1;               // the chunk it came from; -1: a heap of its own
    GpuHeap own{};
};

struct RenderViewEntry
{
    uint32 mip = 0;
    uint32 slice = 0;
    RenderView* view = nullptr;
};

struct TextureSlot
{
    PlacedTexture placed{};
    TextureDesc desc{};             // NoGraphicsAPI's
    gs_ngfx_texture_desc goose{};   // as the program sees it, mips resolved
    uint32 sampled = UINT32_MAX;    // descriptor slots, or UINT32_MAX for none
    uint32 storage = UINT32_MAX;
    std::vector<uint32> storage_mips;   // one level's storage slot, by mip, made on first use
    std::vector<RenderViewEntry> views;
};

struct SamplerSlot
{
    uint32 index = 0;
};

// A pass's targets, which a pipeline is created for.
struct Targets
{
    uint32 count = 0;
    Format color[4] = {Format::undefined, Format::undefined, Format::undefined, Format::undefined};
    Format depth = Format::undefined;
    Format stencil = Format::undefined;

    bool operator==(const Targets& o) const
    {
        if (count != o.count || depth != o.depth || stencil != o.stencil) return false;
        for (uint32 i = 0; i < count; ++i)
            if (color[i] != o.color[i]) return false;
        return true;
    }
};

struct PipelineVariant
{
    Targets targets{};
    PSO* pso = nullptr;
};

struct PipelineSlot
{
    std::vector<uint32> code;       // the blob's code for this backend, word-aligned
    std::string vertex, fragment;
    gs_ngfx_pipeline_desc desc{};
    std::vector<PipelineVariant> variants;
};

struct ComputeSlot
{
    PSO* pso = nullptr;
};

// --- the one state ---------------------------------------------------------------

constexpr uint32 frames_in_flight = 2;
constexpr uint64 frame_memory_size = 16ull << 20;    // per frame in flight
constexpr uint64 buffer_chunk_size = 64ull << 20;
constexpr uint32 buffer_chunk_allocations = 4096;
constexpr uint64 texture_heap_size = 512ull << 20;
constexpr uint32 max_textures = 4096;
constexpr uint32 texture_descriptors = 8192;
constexpr uint32 sampler_descriptors = 1024;
constexpr uint64 upload_capacity = 32ull << 20;
constexpr uint32 delete_capacity = 1u << 16;

// Indices into a descriptor heap: the lowest free one first.
struct SlotAllocator
{
    uint32 capacity = 0;
    uint32 next = 0;
    std::vector<uint32> freed;

    uint32 allocate()
    {
        if (!freed.empty())
        {
            const uint32 i = freed.back();
            freed.pop_back();
            return i;
        }
        return next < capacity ? next++ : UINT32_MAX;
    }

    void free(uint32 i)
    {
        if (i != UINT32_MAX) freed.push_back(i);
    }
};

struct State
{
    Device* device = nullptr;
    bool windowed = false;
    TimelinePoint timeline{};       // value: the latest submission's

    CommandPool* pools[frames_in_flight] = {};
    uint64 pool_values[frames_in_flight] = {};   // the latest submission from each pool
    uint32 slot = 0;                // the frame in flight being recorded
    GpuHeap frame_heap{};
    BumpAllocator* frame_data[frames_in_flight] = {};
    CommandBuffer* commands = nullptr;

    UploadQueue* uploads = nullptr;
    std::vector<GpuRange> uploaded; // destinations in the upload batch being built
    DeleteQueue* deletes = nullptr;
    // Releases since the last submission of the frame's commands, which wait
    // for that submission.
    std::vector<void (*)(void*)> pending_fns;
    std::vector<void*> pending_args;

    std::vector<GpuHeap> buffer_heaps;
    std::vector<HeapAllocator*> buffer_allocators;
    TextureHeap texture_heap{};
    TextureAllocator* texture_allocator = nullptr;
    TextureDescriptorHeap* texture_descriptors = nullptr;
    SamplerDescriptorHeap* sampler_descriptors = nullptr;
    SlotAllocator texture_slots;
    SlotAllocator sampler_slots;

    Table<BufferSlot> buffers{"buffer"};
    Table<TextureSlot> textures{"texture"};
    Table<SamplerSlot> samplers{"sampler"};
    Table<PipelineSlot> pipelines{"pipeline"};
    Table<ComputeSlot> computes{"compute pipeline"};

    // The screen: what a frame draws into, copied to the window at frame().
    gs_ngfx_texture screen{}, screen_depth{};
    int width = 0, height = 0;
    PSO* copy_pso = nullptr;
    uint32 copy_samplers[2] = {};   // nearest, linear
    gs_ngfx_compute_pipeline mip_pipeline{};   // made on first use, kept until close

    // Recording.
    bool in_pass = false;
    Targets pass_targets{};
    uint32 pass_width = 0, pass_height = 0;
    // Bound by handle: a table's slots move when it grows.
    uint32 pipeline = 0;
    PSO* bound_pso = nullptr;
    uint32 compute = 0;
    // The barriers the module owns (docs/design/ngfx.md, "Synchronization"): the frame's
    // first pass gets the frame boundary's, and a pass drawing into a target
    // an earlier pass drew into since then gets the same-target one.
    bool frame_barrier = false;
    std::vector<uint32> written_targets;
    // GOOSE_NGFX_SYNC=full: a full barrier around every pass and dispatch, to
    // find a missing program barrier by comparing output.
    bool full_sync = false;
};

extern State g;

// --- shared helpers ----------------------------------------------------------------

// The frame's command buffer, begun with the descriptor heaps set.
CommandBuffer* frame_commands();
// Submits the frame's commands so far, after the setup and upload work before
// them; `present` with submit_and_present. Starts no new command buffer.
void submit_frame_commands(bool present);
// Submits what has been recorded, waits for all of it, and begins recording
// again in the same frame.
void finish();
// A command buffer of its own for recording texture initialization, submitted
// at once by end_setup, before any upload that could reach the texture.
CommandBuffer* begin_setup();
void end_setup(CommandBuffer* commands);
// Runs `fn(arg)` once the GPU has finished every use recorded so far.
void defer_release(void (*fn)(void*), void* arg);
// Uploads `size` bytes to `destination`, flushing the batch first when an
// earlier upload in it overlaps (upload_queue.hpp: flush between overlapping
// transfer writes).
void upload(GpuRange destination, const void* data, uint64 size);
void flush_uploads();

bool texture_format(int32 goose, Format* out);
TextureSlot* texture_slot(gs_ngfx_texture t);
RenderView* render_view(TextureSlot* s, uint32 mip, uint32 slice);
// Creates a texture: placed, initialized, with its descriptors written.
bool create_texture(const gs_ngfx_texture_desc& desc, uint32* id, TextureSlot** out);
void free_texture(TextureSlot& s);
// Frees a copy of `s` once the GPU has finished every use recorded so far.
void release_texture_later(const TextureSlot& s);
// examples/deferred_renderer's barrier before its G-buffer pass: earlier
// target writes and fragment reads before a pass writes targets.
void frame_boundary_barrier(CommandBuffer* commands);
// A shader blob's code as 4-byte-aligned words: blobs sit in static data with
// no alignment, and NoGraphicsAPI takes SPIR-V as words.
std::vector<uint32> aligned_words(const void* data, size_t size);
// gfx's COMPARE_* are SDL's, one above NoGraphicsAPI's CompareOp.
inline CompareOp compare_op(int32 c) { return c >= 1 && c <= 8 ? CompareOp(c - 1) : CompareOp::less; }

// The code section and entries of an embed_slang blob, checked.
bool read_blob(gs_ngfx_bytes blob, const char* what, std::vector<uint32>* code,
               std::vector<gs_ngfx_blob_entry>* entries);
PSO* create_copy_pso();
void release_pipelines();

// GOOSE_NGFX_SYNC=full's barrier: every earlier write before every later
// access.
void full_sync_barrier(CommandBuffer* commands);
// The barrier before the GPU reads a texture or buffer back (ngfx_draw.cpp).
void barriers_before_readback();
void barriers_after_texture_readback();

} // namespace ngfx
