// The ngfx layer's resources: buffers (GPU-only memory, written through the
// upload queue), this frame's data (mapped memory from a BumpAllocator),
// textures placed in one texture heap with their descriptors written, and
// samplers. Read-back copies into a readback heap and waits.

#include "ngfx_gpu.hpp"

namespace ngfx {

namespace {

int32 mip_size(int32 size, int32 mip)
{
    size >>= mip;
    return size < 1 ? 1 : size;
}

bool is_depth(Format f) { return get_texture_format_info(f).depth || get_texture_format_info(f).stencil; }

BufferSlot* buffer_slot(gs_ngfx_buffer b) { return g.buffers.get(b.id); }

// GPU memory for a buffer: a suballocation of a 64 MB chunk, or a heap of its
// own for a larger one.
bool allocate_buffer(BufferSlot& s, uint64 size)
{
    const uint64 rounded = (size + 15) & ~uint64(15);
    if (rounded > buffer_chunk_size / 2)
    {
        s.own = create_gpu_heap(g.device, rounded, MemoryType::gpu_only);
        if (!s.own.owner) return ngfx_fail("out of GPU memory for a buffer of %llu bytes", (unsigned long long)size);
        s.range = {.gpu = s.own.range.gpu, .size = size};
        s.chunk = -1;
        return true;
    }
    for (size_t i = 0; i < g.buffer_allocators.size(); ++i)
    {
        const HeapAllocation<byte> a = g.buffer_allocators[i]->allocate(rounded);
        if (a.range.size)
        {
            s.allocation = a;
            s.chunk = int32(i);
            s.range = {.gpu = a.range.gpu, .size = size};
            return true;
        }
    }
    const GpuHeap heap = create_gpu_heap(g.device, buffer_chunk_size, MemoryType::gpu_only);
    if (!heap.owner) return ngfx_fail("out of GPU memory for buffers");
    g.buffer_heaps.push_back(heap);
    g.buffer_allocators.push_back(new HeapAllocator(heap.range, buffer_chunk_allocations));
    const HeapAllocation<byte> a = g.buffer_allocators.back()->allocate(rounded);
    if (!a.range.size) return ngfx_fail("out of GPU memory for buffers");
    s.allocation = a;
    s.chunk = int32(g.buffer_allocators.size() - 1);
    s.range = {.gpu = a.range.gpu, .size = size};
    return true;
}

void free_buffer(void* p)
{
    BufferSlot* s = static_cast<BufferSlot*>(p);
    if (s->chunk < 0) destroy_gpu_heap(s->own);
    else g.buffer_allocators[size_t(s->chunk)]->free(s->allocation);
    delete s;
}

// Copies `size` bytes at `source` back into `out`: the frame's work so far
// submitted and waited for first, then the copy in a submission of its own,
// as tests/texture_copy_test.cpp reads back (its timeline wait between the two
// is also a known NVIDIA driver's need, docs/known-driver-issues.md).
template<typename RecordCopy>
bool read_back(uint64 size, void* out, RecordCopy record_copy)
{
    const GpuHeap readback = create_gpu_heap(g.device, (size + 15) & ~uint64(15), MemoryType::readback);
    if (!readback.owner) return ngfx_fail("out of memory for reading back %llu bytes", (unsigned long long)size);
    finish();
    CommandBuffer* commands = frame_commands();
    barriers_before_readback();
    record_copy(commands, gpu_range(readback));
    // tests/root_data_test.cpp: the copy, then to the host.
    barrier(commands, Stage::transfer, Access::transfer_write, Stage::host, Access::host_read);
    finish();
    memcpy(out, readback.range.cpu, size_t(size));
    destroy_gpu_heap(readback);
    return true;
}

} // namespace

bool texture_format(int32 goose, Format* out)
{
    switch (goose)
    {
    case GS_NGFX_RGBA8: *out = Format::rgba8_unorm; return true;
    case GS_NGFX_BGRA8: *out = Format::bgra8_unorm; return true;
    case GS_NGFX_RGBA8_SRGB: *out = Format::rgba8_srgb; return true;
    case GS_NGFX_R8: *out = Format::r8_unorm; return true;
    case GS_NGFX_RG8: *out = Format::rg8_unorm; return true;
    case GS_NGFX_RGBA16F: *out = Format::rgba16_float; return true;
    case GS_NGFX_RGBA32F: *out = Format::rgba32_float; return true;
    case GS_NGFX_R16F: *out = Format::r16_float; return true;
    case GS_NGFX_RG16F: *out = Format::rg16_float; return true;
    case GS_NGFX_R32F: *out = Format::r32_float; return true;
    case GS_NGFX_RG32F: *out = Format::rg32_float; return true;
    case GS_NGFX_R32UI: *out = Format::r32_uint; return true;
    case GS_NGFX_RGBA8UI: *out = Format::rgba8_uint; return true;
    case GS_NGFX_RGB10A2: *out = Format::rgb10a2_unorm; return true;
    case GS_NGFX_RG11B10F: *out = Format::rg11b10_float; return true;
    case GS_NGFX_DEPTH16: *out = Format::d16_unorm; return true;
    case GS_NGFX_DEPTH32F:
    case GS_NGFX_DEPTH: *out = Format::d32_float; return true;
    case GS_NGFX_DEPTH24_STENCIL8: *out = Format::d24_unorm_s8_uint; return true;
    default: return false;
    }
}

TextureSlot* texture_slot(gs_ngfx_texture t) { return g.textures.get(t.id); }

RenderView* render_view(TextureSlot* s, uint32 mip, uint32 slice)
{
    for (const RenderViewEntry& e : s->views)
        if (e.mip == mip && e.slice == slice) return e.view;
    RenderView* view = create_render_view(s->placed.texture, {.mip_level = mip, .slice = slice});
    if (view) s->views.push_back({.mip = mip, .slice = slice, .view = view});
    return view;
}

void free_texture(TextureSlot& s)
{
    for (const RenderViewEntry& e : s.views) destroy_render_view(e.view);
    s.views.clear();
    g.texture_allocator->free(s.placed);
    g.texture_slots.free(s.sampled);
    g.texture_slots.free(s.storage);
    for (uint32 slot : s.storage_mips) g.texture_slots.free(slot);
}

void release_texture_later(const TextureSlot& s)
{
    defer_release([](void* p) {
        TextureSlot* slot = static_cast<TextureSlot*>(p);
        free_texture(*slot);
        delete slot;
    }, new TextureSlot(s));
}

bool create_texture(const gs_ngfx_texture_desc& in, uint32* id, TextureSlot** out)
{
    gs_ngfx_texture_desc d = in;
    Format format;
    if (!texture_format(d.format, &format)) return ngfx_misuse("texture format %d is not one of ngfx's formats", d.format);
    if (d.kind < GS_NGFX_TEXTURE_2D || d.kind > GS_NGFX_TEXTURE_CUBE)
        return ngfx_misuse("texture kind %d is not one of TEXTURE_2D, _2D_ARRAY, _3D or _CUBE", d.kind);
    if (d.width < 1 || d.height < 1 || d.width > 16384 || d.height > 16384)
        return ngfx_misuse("a texture of %d x %d", d.width, d.height);
    if (d.kind == GS_NGFX_TEXTURE_CUBE && d.width != d.height)
        return ngfx_misuse("a cube texture must be square, not %d x %d", d.width, d.height);
    if (d.kind == GS_NGFX_TEXTURE_2D || d.kind == GS_NGFX_TEXTURE_CUBE) d.depth = 1;
    if (d.depth < 1 || d.depth > 2048)
        return ngfx_misuse("a texture %s of %d", d.kind == GS_NGFX_TEXTURE_3D ? "depth" : "layer count", d.depth);
    int32 largest = d.width > d.height ? d.width : d.height;
    if (d.kind == GS_NGFX_TEXTURE_3D && d.depth > largest) largest = d.depth;
    int32 chain = 1;
    while (largest >> chain) chain++;
    if (d.mips <= 0 || d.mips > chain) d.mips = chain;
    const uint32 usage = uint32(d.usage);
    if (!usage || (usage & ~15u)) return ngfx_misuse("texture usage %d is not a combination of the usage flags", d.usage);
    const bool depth = is_depth(format);
    if (depth != ((usage & GS_NGFX_DEPTH_TARGET) != 0))
        return ngfx_misuse(depth ? "a depth format needs DEPTH_TARGET usage" : "DEPTH_TARGET usage needs a depth format");

    // Every texture can be updated and read back, as gfx's can.
    TextureUsage nu = TextureUsage::transfer_source | TextureUsage::transfer_destination;
    if (usage & GS_NGFX_SAMPLED) nu = nu | TextureUsage::sampled;
    if (usage & GS_NGFX_COLOR_TARGET) nu = nu | TextureUsage::color_attachment;
    if (usage & GS_NGFX_DEPTH_TARGET) nu = nu | TextureUsage::depth_stencil_attachment;
    if (usage & GS_NGFX_STORAGE) nu = nu | TextureUsage::storage;
    if (!supports_texture_format(g.device, format, nu))
        return ngfx_fail("this device does not support texture format %d with usage %d", d.format, d.usage);
    static const TextureType types[] = {TextureType::two_d, TextureType::two_d_array, TextureType::three_d, TextureType::cube};
    TextureDesc nd{.type = types[d.kind],
                   .extent = {.x = uint32(d.width), .y = uint32(d.height),
                              .z = d.kind == GS_NGFX_TEXTURE_3D ? uint32(d.depth) : 1u},
                   .mip_levels = uint32(d.mips),
                   .layer_count = d.kind == GS_NGFX_TEXTURE_CUBE ? 6u
                                : d.kind == GS_NGFX_TEXTURE_2D_ARRAY ? uint32(d.depth) : 1u,
                   .format = format,
                   .usage = nu};

    // Texture initialization in a submission of its own, before any upload
    // can reach it, as examples/cube submits its texture's before uploading.
    CommandBuffer* setup = begin_setup();
    const PlacedTexture placed = g.texture_allocator->allocate(setup, nd);
    end_setup(setup);
    if (!placed.texture) return ngfx_fail("out of texture memory for a %d x %d texture", d.width, d.height);

    TextureSlot* s = g.textures.add(*id);
    if (!s)
    {
        PlacedTexture p = placed;
        g.texture_allocator->free(p);
        return false;
    }
    s->placed = placed;
    s->desc = nd;
    s->goose = d;
    if (usage & GS_NGFX_SAMPLED)
    {
        s->sampled = g.texture_slots.allocate();
        if (s->sampled == UINT32_MAX) return ngfx_fail("out of texture descriptors");
        write_texture_descriptor(g.texture_descriptors, s->sampled, placed.texture, TextureDescriptorType::sampled);
    }
    if (usage & GS_NGFX_STORAGE)
    {
        s->storage = g.texture_slots.allocate();
        if (s->storage == UINT32_MAX) return ngfx_fail("out of texture descriptors");
        write_texture_descriptor(g.texture_descriptors, s->storage, placed.texture, TextureDescriptorType::storage);
    }
    *out = s;
    return true;
}

} // namespace ngfx

using namespace ngfx;

namespace {

// The region of `s` that `r` names, with w == 0 as the whole mip, as gfx's.
bool region_of(TextureSlot* s, const gs_ngfx_region* r, TextureCopyDesc* out, const char* what)
{
    const gs_ngfx_texture_desc& d = s->goose;
    if (r->mip < 0 || r->mip >= d.mips) return ngfx_misuse("%s: mip %d of a texture with %d", what, r->mip, d.mips);
    const int32 layers = d.kind == GS_NGFX_TEXTURE_CUBE ? 6 : d.kind == GS_NGFX_TEXTURE_2D_ARRAY ? d.depth : 1;
    if (r->layer < 0 || r->layer >= layers) return ngfx_misuse("%s: layer %d of a texture with %d", what, r->layer, layers);
    const int32 mw = mip_size(d.width, r->mip), mh = mip_size(d.height, r->mip);
    const int32 md = d.kind == GS_NGFX_TEXTURE_3D ? mip_size(d.depth, r->mip) : 1;
    *out = TextureCopyDesc{.mip_level = uint32(r->mip), .base_slice = uint32(r->layer), .slice_count = 1};
    if (r->w == 0)
    {
        out->extent = {.x = uint32(mw), .y = uint32(mh), .z = uint32(md)};
        return true;
    }
    const int32 h = r->h ? r->h : 1, dd = r->d ? r->d : 1;
    if (r->x < 0 || r->y < 0 || r->z < 0 || r->w < 0 || h < 0 || dd < 0 || r->x + r->w > mw || r->y + h > mh ||
        r->z + dd > md)
        return ngfx_misuse("%s: region %d,%d,%d + %d x %d x %d is outside the %d x %d x %d mip", what, r->x, r->y, r->z,
                           r->w, h, dd, mw, mh, md);
    out->offset = {.x = uint32(r->x), .y = uint32(r->y), .z = uint32(r->z)};
    out->extent = {.x = uint32(r->w), .y = uint32(h), .z = uint32(dd)};
    return true;
}

bool region_bytes(TextureSlot* s, const TextureCopyDesc& c, int64_t len, const char* what)
{
    const int64_t want = int64_t(get_texture_format_info(s->desc.format).bytes_per_block) * c.extent.x * c.extent.y *
                         c.extent.z;
    if (len != want)
        return ngfx_misuse("%s: %lld bytes for a %u x %u x %u region of %lld", what, (long long)len, c.extent.x, c.extent.y,
                           c.extent.z, (long long)want);
    return true;
}

bool readable(TextureSlot* s, const char* what)
{
    if (is_depth(s->desc.format)) return ngfx_misuse("%s: a depth texture cannot be read back", what);
    return true;
}

bool read_texture_region(TextureSlot* s, const TextureCopyDesc& c, void* out, uint64 size)
{
    Texture* texture = s->placed.texture;
    return read_back(size, out, [&](CommandBuffer* commands, GpuRange destination) {
        copy_texture_to_memory(commands, texture, destination, c);
        barriers_after_texture_readback();
    });
}

} // namespace

extern "C" {

gs_ngfx_buffer gs_ngfx_create_buffer(int64_t size, gs_ngfx_bytes data)
{
    gs_ngfx_buffer h{0};
    if (!ngfx_need_device("buffer")) return h;
    if (size < 1 || size > (int64_t(1) << 40) || data.len > size)
    {
        ngfx_misuse("a buffer of %lld bytes holding %lld", (long long)size, (long long)data.len);
        return h;
    }
    BufferSlot fresh{};
    if (!allocate_buffer(fresh, uint64(size))) return h;
    BufferSlot* s = g.buffers.add(h.id);
    if (!s) return h;
    *s = fresh;
    // The data, then zeros: a fresh allocation holds whatever was there.
    std::vector<byte> bytes(static_cast<size_t>(size), 0);
    if (data.len > 0) memcpy(bytes.data(), data.data, size_t(data.len));
    upload(s->range, bytes.data(), uint64(size));
    return h;
}

uint8_t gs_ngfx_update_buffer(gs_ngfx_buffer b, int64_t offset, gs_ngfx_bytes data)
{
    if (!ngfx_need_device("update_buffer")) return 0;
    BufferSlot* s = buffer_slot(b);
    if (!s) return 0;
    if (offset < 0 || data.len < 0 || uint64(offset + data.len) > s->range.size)
        return ngfx_misuse("update_buffer: %lld bytes at %lld of a buffer of %llu", (long long)data.len, (long long)offset,
                           (unsigned long long)s->range.size);
    if (data.len) upload({.gpu = static_cast<byte*>(s->range.gpu) + offset, .size = uint64(data.len)}, data.data, uint64(data.len));
    return 1;
}

uint8_t gs_ngfx_read_buffer(gs_ngfx_buffer b, int64_t offset, gs_ngfx_bytes out)
{
    if (!ngfx_need_device("read_buffer")) return 0;
    if (g.in_pass) return ngfx_misuse("read_buffer inside a pass: end the pass first");
    BufferSlot* s = buffer_slot(b);
    if (!s) return 0;
    if (offset < 0 || out.len < 0 || uint64(offset + out.len) > s->range.size)
        return ngfx_misuse("read_buffer: %lld bytes at %lld of a buffer of %llu", (long long)out.len, (long long)offset,
                           (unsigned long long)s->range.size);
    if (!out.len) return 1;
    const GpuRange source{.gpu = static_cast<byte*>(s->range.gpu) + offset, .size = uint64(out.len)};
    return read_back(uint64(out.len), out.data, [&](CommandBuffer* commands, GpuRange destination) {
        copy_memory(commands, source, {.gpu = destination.gpu, .size = source.size});
    });
}

int64_t gs_ngfx_buffer_size(gs_ngfx_buffer b)
{
    BufferSlot* s = g.buffers.find(b.id);
    return s ? int64_t(s->range.size) : 0;
}

uint64_t gs_ngfx_buffer_address(gs_ngfx_buffer b)
{
    if (!ngfx_need_device("address")) return 0;
    BufferSlot* s = buffer_slot(b);
    return s ? uint64_t(reinterpret_cast<uintptr_t>(s->range.gpu)) : 0;
}

void gs_ngfx_release_buffer(gs_ngfx_buffer b)
{
    if (!ngfx_need_device("release_buffer")) return;
    BufferSlot* s = buffer_slot(b);
    if (!s) return;
    defer_release(free_buffer, new BufferSlot(*s));
    g.buffers.remove(b.id);
}

uint64_t gs_ngfx_frame_data(gs_ngfx_bytes data)
{
    if (!ngfx_need_device("frame_data")) return 0;
    if (data.len <= 0)
    {
        ngfx_misuse("frame_data: no bytes");
        return 0;
    }
    const GpuCpuRange<byte> r = g.frame_data[g.slot]->allocate(uint64(data.len));
    if (!r.size)
    {
        ngfx_misuse("frame_data: this frame's %llu MB of data are used up", (unsigned long long)(frame_memory_size >> 20));
        return 0;
    }
    memcpy(r.cpu, data.data, size_t(data.len));
    return uint64_t(reinterpret_cast<uintptr_t>(r.gpu));
}

gs_ngfx_texture gs_ngfx_create_texture(const gs_ngfx_texture_desc* desc)
{
    gs_ngfx_texture h{0};
    if (!ngfx_need_device("texture")) return h;
    TextureSlot* s;
    if (!create_texture(*desc, &h.id, &s)) h.id = 0;
    return h;
}

gs_ngfx_texture gs_ngfx_load_texture(gs_ngfx_bytes path, int64_t flags)
{
    gs_ngfx_texture h{0};
    if (!ngfx_need_device("load_texture")) return h;
    char buf[1024];
    uint8_t* rgba = nullptr;
    int w = 0, hh = 0;
    if (!ngfx_png_load(ngfx_cstr(path, buf, sizeof buf), &rgba, &w, &hh)) return h;
    const gs_ngfx_texture_desc d{.kind = GS_NGFX_TEXTURE_2D,
                                 .format = (flags & GS_NGFX_LOAD_SRGB) ? GS_NGFX_RGBA8_SRGB : GS_NGFX_RGBA8,
                                 .usage = GS_NGFX_SAMPLED, .width = w, .height = hh, .depth = 1, .mips = 1};
    h = gs_ngfx_create_texture(&d);
    if (h.id)
    {
        const gs_ngfx_region r{};
        gs_ngfx_update_texture(h, &r, {rgba, int64_t(w) * hh * 4});
    }
    ngfx_png_free(rgba);
    return h;
}

uint8_t gs_ngfx_update_texture(gs_ngfx_texture t, const gs_ngfx_region* region, gs_ngfx_bytes data)
{
    if (!ngfx_need_device("update_texture")) return 0;
    TextureSlot* s = texture_slot(t);
    if (!s) return 0;
    TextureCopyDesc c;
    if (!region_of(s, region, &c, "update_texture") || !region_bytes(s, c, data.len, "update_texture")) return 0;
    if (is_depth(s->desc.format)) return ngfx_misuse("update_texture: a depth texture");
    // The texture-region helper splits what does not fit the staging memory.
    flush_uploads();
    gpu::upload_texture(*g.uploads, s->placed.texture, s->desc, ByteSpan(data.data, size_t(data.len)), c);
    flush_uploads();
    return 1;
}

uint8_t gs_ngfx_read_texture(gs_ngfx_texture t, const gs_ngfx_region* region, gs_ngfx_bytes out)
{
    if (!ngfx_need_device("read_texture")) return 0;
    if (g.in_pass) return ngfx_misuse("read_texture inside a pass: end the pass first");
    TextureSlot* s = texture_slot(t);
    if (!s || !readable(s, "read_texture")) return 0;
    TextureCopyDesc c;
    if (!region_of(s, region, &c, "read_texture") || !region_bytes(s, c, out.len, "read_texture")) return 0;
    return read_texture_region(s, c, out.data, uint64(out.len));
}

uint8_t gs_ngfx_save_png(gs_ngfx_texture t, gs_ngfx_bytes path)
{
    if (!ngfx_need_device("save_png")) return 0;
    if (g.in_pass) return ngfx_misuse("save_png inside a pass: end the pass first");
    TextureSlot* s = texture_slot(t);
    if (!s || !readable(s, "save_png")) return 0;
    const Format f = s->desc.format;
    if (f != Format::rgba8_unorm && f != Format::rgba8_srgb && f != Format::bgra8_unorm)
        return ngfx_misuse("save_png: only an RGBA8, RGBA8_SRGB or BGRA8 texture");
    const gs_ngfx_region r{};
    TextureCopyDesc c;
    if (!region_of(s, &r, &c, "save_png")) return 0;
    const uint64 len = uint64(c.extent.x) * c.extent.y * 4;
    std::vector<uint8_t> pixels(static_cast<size_t>(len));
    const bool screen = t.id == g.screen.id;
    if (!read_texture_region(s, c, pixels.data(), len)) return 0;
    // The screen is saved as it is shown: opaque, as gfx saves it.
    if (screen)
        for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
    char buf[1024];
    return ngfx_png_save(ngfx_cstr(path, buf, sizeof buf), pixels.data(), int(c.extent.x), int(c.extent.y),
                         f == Format::bgra8_unorm);
}

uint8_t gs_ngfx_texture_info(gs_ngfx_texture t, gs_ngfx_texture_desc* out)
{
    TextureSlot* s = g.textures.find(t.id);
    if (!s) return 0;
    *out = s->goose;
    return 1;
}

uint32_t gs_ngfx_texture_index(gs_ngfx_texture t)
{
    if (!ngfx_need_device("index")) return 0;
    TextureSlot* s = texture_slot(t);
    if (!s) return 0;
    if (s->sampled == UINT32_MAX)
    {
        ngfx_misuse("index: a texture made without SAMPLED usage");
        return 0;
    }
    return s->sampled;
}

uint32_t gs_ngfx_storage_index(gs_ngfx_texture t)
{
    if (!ngfx_need_device("storage_index")) return 0;
    TextureSlot* s = texture_slot(t);
    if (!s) return 0;
    if (s->storage == UINT32_MAX)
    {
        ngfx_misuse("storage_index: a texture made without STORAGE usage");
        return 0;
    }
    return s->storage;
}

uint32_t gs_ngfx_storage_mip_index(gs_ngfx_texture t, int64_t mip)
{
    if (!ngfx_need_device("storage_index")) return 0;
    TextureSlot* s = texture_slot(t);
    if (!s) return 0;
    if (s->storage == UINT32_MAX)
    {
        ngfx_misuse("storage_index: a texture made without STORAGE usage");
        return 0;
    }
    if (mip < 0 || mip >= s->goose.mips)
    {
        ngfx_misuse("storage_index: mip %lld of a texture with %d", (long long)mip, s->goose.mips);
        return 0;
    }
    if (s->storage_mips.empty()) s->storage_mips.assign(size_t(s->goose.mips), UINT32_MAX);
    uint32& slot = s->storage_mips[size_t(mip)];
    if (slot == UINT32_MAX)
    {
        slot = g.texture_slots.allocate();
        if (slot == UINT32_MAX)
        {
            ngfx_fail("out of texture descriptors");
            return 0;
        }
        write_texture_descriptor(g.texture_descriptors, slot, s->placed.texture, TextureDescriptorType::storage,
                                 {.base_mip = uint32(mip), .mip_count = 1});
    }
    return slot;
}

void gs_ngfx_release_texture(gs_ngfx_texture t)
{
    if (!ngfx_need_device("release_texture")) return;
    if (t.id == g.screen.id || t.id == g.screen_depth.id)
    {
        ngfx_misuse("release_texture: the screen belongs to ngfx");
        return;
    }
    TextureSlot* s = texture_slot(t);
    if (!s) return;
    release_texture_later(*s);
    g.textures.remove(t.id);
}

gs_ngfx_sampler gs_ngfx_create_sampler(const gs_ngfx_sampler_desc* desc)
{
    gs_ngfx_sampler h{0};
    if (!ngfx_need_device("sampler")) return h;
    const gs_ngfx_sampler_desc& d = *desc;
    auto filter = [](int32 f) { return f == GS_NGFX_NEAREST ? Filter::nearest : Filter::linear; };
    auto wrap = [](int32 w) {
        return w == GS_NGFX_MIRROR ? AddressMode::mirrored_repeat : w == GS_NGFX_CLAMP ? AddressMode::clamp_to_edge : AddressMode::repeat;
    };
    if (uint32(d.min_filter) > 1 || uint32(d.mag_filter) > 1 || uint32(d.mip_filter) > 1 || uint32(d.wrap_u) > 2 ||
        uint32(d.wrap_v) > 2 || uint32(d.wrap_w) > 2 || uint32(d.compare) > 8)
    {
        ngfx_misuse("a SamplerDesc with a filter, wrap or compare that is not one of the constants");
        return h;
    }
    const uint32 index = g.sampler_slots.allocate();
    if (index == UINT32_MAX)
    {
        ngfx_fail("out of sampler descriptors");
        return h;
    }
    SamplerSlot* s = g.samplers.add(h.id);
    if (!s)
    {
        g.sampler_slots.free(index);
        return h;
    }
    s->index = index;
    write_sampler_descriptor(g.sampler_descriptors, index,
                             {.min_filter = filter(d.min_filter), .mag_filter = filter(d.mag_filter),
                              .mip_filter = filter(d.mip_filter), .address_u = wrap(d.wrap_u), .address_v = wrap(d.wrap_v),
                              .address_w = wrap(d.wrap_w), .anisotropic = d.max_anisotropy > 1.0f,
                              .compare_enabled = d.compare != 0,
                              .compare = compare_op(d.compare)});
    return h;
}

uint32_t gs_ngfx_sampler_index(gs_ngfx_sampler smp)
{
    if (!ngfx_need_device("index")) return 0;
    SamplerSlot* s = g.samplers.get(smp.id);
    return s ? s->index : 0;
}

void gs_ngfx_release_sampler(gs_ngfx_sampler smp)
{
    if (!ngfx_need_device("release_sampler")) return;
    SamplerSlot* s = g.samplers.get(smp.id);
    if (!s) return;
    defer_release([](void* p) { g.sampler_slots.free(uint32(reinterpret_cast<uintptr_t>(p))); },
                  reinterpret_cast<void*>(uintptr_t(s->index)));
    g.samplers.remove(smp.id);
}

} // extern "C"
