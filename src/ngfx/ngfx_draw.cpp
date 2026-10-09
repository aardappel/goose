// The ngfx layer's pipelines, passes, draws and dispatches, and the barriers
// the layer records around them: the frame boundary, a target drawn into
// twice, the screen copy, the program's own (gs_ngfx_barrier), and
// GOOSE_NGFX_SYNC=full's (docs/design/ngfx.md, "Synchronization").

#include "ngfx_gpu.hpp"

#include "ngfx_mips_code.h"
#include "ngfx_screen_code.h"

namespace ngfx {

namespace {

BlendState blend_state(int32 mode)
{
    BlendState b{.enabled = mode != GS_NGFX_BLEND_NONE};
    switch (mode)
    {
    case GS_NGFX_BLEND_ALPHA:
        b.color = {.source = BlendFactor::source_alpha, .destination = BlendFactor::one_minus_source_alpha};
        b.alpha = {.source = BlendFactor::one, .destination = BlendFactor::one_minus_source_alpha};
        break;
    case GS_NGFX_BLEND_ADD:
        b.color = {.source = BlendFactor::source_alpha, .destination = BlendFactor::one};
        b.alpha = {.source = BlendFactor::zero, .destination = BlendFactor::one};
        break;
    case GS_NGFX_BLEND_PREMULTIPLIED:
        b.color = {.source = BlendFactor::one, .destination = BlendFactor::one_minus_source_alpha};
        b.alpha = {.source = BlendFactor::one, .destination = BlendFactor::one_minus_source_alpha};
        break;
    case GS_NGFX_BLEND_MULTIPLY:
        b.color = {.source = BlendFactor::destination_color, .destination = BlendFactor::zero};
        b.alpha = {.source = BlendFactor::zero, .destination = BlendFactor::one};
        break;
    default: break;
    }
    return b;
}

// NoGraphicsAPI culls by winding in its y-down clip space, which is the
// winding seen on the screen: front faces wind counterclockwise unless
// `clockwise`, and back faces are the others.
CullMode cull_mode(int32 cull, bool clockwise)
{
    if (cull == GS_NGFX_CULL_NONE) return CullMode::none;
    const bool cull_cw = (cull == GS_NGFX_CULL_BACK) != clockwise;
    return cull_cw ? CullMode::clockwise : CullMode::counter_clockwise;
}

PSO* pipeline_for(PipelineSlot* p, const Targets& t)
{
    for (const PipelineVariant& v : p->variants)
        if (v.targets == t) return v.pso;
    ColorTargetDesc colors[4];
    for (uint32 i = 0; i < t.count; ++i) colors[i] = {.format = t.color[i], .blend = blend_state(p->desc.blend)};
    const ByteSpan code(p->code.data(), p->code.size() * 4);
    PSO* pso = create_graphics_pso(g.device, {
        .vertex = {.code = code, .entry_point = p->vertex.c_str()},
        .fragment = {.code = code, .entry_point = p->fragment.c_str()},
        .color_targets = {colors, t.count},
        .depth_format = t.depth,
        .stencil_format = t.stencil,
        .rasterization = {.cull = cull_mode(p->desc.cull, p->desc.clockwise != 0),
                          .depth_bias_constant = p->desc.depth_bias,
                          .depth_bias_slope = p->desc.depth_bias_slope},
    });
    if (!pso)
    {
        ngfx_fail("creating a pipeline for vertex %s and fragment %s: %s", p->vertex.c_str(), p->fragment.c_str(),
                  ngfx_error_text());
        return nullptr;
    }
    p->variants.push_back({.targets = t, .pso = pso});
    return pso;
}

// Binds the pipeline for this pass's targets, and the depth state that goes
// with it (begin_render_pass resets it).
bool bind_for_draw(const char* what)
{
    if (!g.in_pass) return ngfx_misuse("%s outside a pass: begin_pass first", what);
    if (!g.pipeline) return ngfx_misuse("%s with no pipeline bound", what);
    PipelineSlot* p = g.pipelines.get(g.pipeline);
    if (!p) return false;
    PSO* pso = pipeline_for(p, g.pass_targets);
    if (!pso) return ngfx_misuse("%s: the pipeline cannot draw into this pass's targets: %s", what, ngfx_error_text());
    if (pso != g.bound_pso)
    {
        bind_pso(g.commands, pso);
        g.bound_pso = pso;
        set_depth_stencil(g.commands, {.depth_test = p->desc.depth_test != 0, .depth_write = p->desc.depth_write != 0,
                                       .depth_compare = compare_op(p->desc.depth_compare)});
    }
    return true;
}

std::string entry_name(gs_ngfx_bytes s)
{
    return std::string(reinterpret_cast<const char*>(s.data), s.len > 0 ? size_t(s.len) : 0);
}

// The one entry of `stage`, or the one named `name`.
const gs_ngfx_blob_entry* find_entry(const std::vector<gs_ngfx_blob_entry>& entries, uint32 stage, const std::string& name,
                                     const char* what, const char* stage_name)
{
    const gs_ngfx_blob_entry* found = nullptr;
    int count = 0;
    for (const gs_ngfx_blob_entry& e : entries)
    {
        if (e.stage != stage) continue;
        if (!name.empty() && name != e.name) continue;
        found = &e;
        ++count;
    }
    if (!found)
    {
        if (name.empty()) ngfx_misuse("%s: the shader has no %s entry point", what, stage_name);
        else ngfx_misuse("%s: the shader has no %s entry point named %s", what, stage_name, name.c_str());
        return nullptr;
    }
    if (count > 1)
    {
        ngfx_misuse("%s: the shader has %d %s entry points; name the one to use", what, count, stage_name);
        return nullptr;
    }
    return found;
}

} // namespace

bool read_blob(gs_ngfx_bytes blob, const char* what, std::vector<uint32>* code, std::vector<gs_ngfx_blob_entry>* entries)
{
    gs_ngfx_blob_header h;
    if (blob.len < int64_t(sizeof h)) return ngfx_misuse("%s: not a shader from embed_slang", what);
    memcpy(&h, blob.data, sizeof h);
    if (h.magic != GS_NGFX_BLOB_MAGIC || h.version != GS_NGFX_BLOB_VERSION ||
        uint64(h.metallib_offset) + h.metallib_size > uint64(blob.len) ||
        uint64(h.spirv_offset) + h.spirv_size > uint64(blob.len) ||
        sizeof h + uint64(h.entry_count) * sizeof(gs_ngfx_blob_entry) > uint64(blob.len))
        return ngfx_misuse("%s: not a shader from embed_slang", what);
    entries->resize(h.entry_count);
    memcpy(entries->data(), blob.data + sizeof h, h.entry_count * sizeof(gs_ngfx_blob_entry));
#if defined(__APPLE__)
    const uint32 offset = h.metallib_offset, size = h.metallib_size;
    if (!size) return ngfx_fail("%s: the shader has no metallib (the program was compiled off a Mac)", what);
#else
    const uint32 offset = h.spirv_offset, size = h.spirv_size;
    if (!size) return ngfx_fail("%s: the shader has no SPIR-V", what);
#endif
    *code = aligned_words(blob.data + offset, size);
    return true;
}

void frame_boundary_barrier(CommandBuffer* commands)
{
    barrier(commands, Stage::fragment | Stage::color_output | Stage::depth_stencil_tests,
            Access::shader_read | Access::color_write | Access::depth_stencil_write,
            Stage::color_output | Stage::depth_stencil_tests, Access::color_write | Access::depth_stencil_write);
}

std::vector<uint32> aligned_words(const void* data, size_t size)
{
    std::vector<uint32> words((size + 3) / 4);
    memcpy(words.data(), data, size);
    return words;
}

PSO* create_copy_pso()
{
    const std::vector<uint32> words = aligned_words(ngfx_screen_code, sizeof ngfx_screen_code);
    const ByteSpan aligned(words.data(), sizeof ngfx_screen_code);
    const ColorTargetDesc colors[] = {{.format = Format::bgra8_unorm}};
    PSO* pso = create_graphics_pso(g.device, {
        .vertex = {.code = aligned, .entry_point = "copyVertex"},
        .fragment = {.code = aligned, .entry_point = "copyFragment"},
        .color_targets = colors,
    });
    if (!pso) ngfx_fail("creating the screen's copy pipeline: %s", ngfx_error_text());
    return pso;
}

void release_pipelines()
{
    g.pipelines.each([](PipelineSlot& p) {
        for (const PipelineVariant& v : p.variants) destroy_pso(v.pso);
    });
    g.computes.each([](ComputeSlot& c) { destroy_pso(c.pso); });
    g.pipelines.clear();
    g.computes.clear();
}

void full_sync_barrier(CommandBuffer* commands)
{
    constexpr Access writes = Access::transfer_write | Access::shader_write | Access::color_write | Access::depth_stencil_write;
    constexpr Access all = writes | Access::transfer_read | Access::shader_read | Access::color_read |
                           Access::depth_stencil_read | Access::indirect_read | Access::index_read | Access::descriptor_read;
    barrier(commands, Stage::all_commands, writes, Stage::all_commands, all);
}

void barriers_before_readback()
{
    CommandBuffer* commands = frame_commands();
    // Whatever wrote what is read: a pass (tests/texture_alias_test.cpp), a
    // dispatch (tests/root_data_test.cpp) or an upload
    // (tests/texture_copy_test.cpp), each with its own recorded barrier.
    barrier(commands, Stage::color_output, Access::color_write, Stage::transfer, Access::transfer_read);
    barrier(commands, Stage::compute, Access::shader_write, Stage::transfer, Access::transfer_read);
    barrier(commands, Stage::transfer, Access::transfer_write, Stage::transfer, Access::transfer_read);
}

void barriers_after_texture_readback()
{
    // tests/metal_backend_test.cpp: the copy's reads before the target is
    // drawn into again.
    barrier(frame_commands(), Stage::transfer, Access::transfer_read, Stage::color_output, Access::color_write);
}

} // namespace ngfx

using namespace ngfx;

extern "C" {

gs_ngfx_pipeline gs_ngfx_create_pipeline(gs_ngfx_bytes shader, gs_ngfx_bytes vertex, gs_ngfx_bytes fragment,
                                         const gs_ngfx_pipeline_desc* desc)
{
    gs_ngfx_pipeline h{0};
    if (!ngfx_need_device("pipeline")) return h;
    const gs_ngfx_pipeline_desc& d = *desc;
    if (uint32(d.cull) > 2 || uint32(d.blend) > 4 || uint32(d.depth_compare) > 8)
    {
        ngfx_misuse("a PipelineDesc with a cull, blend or compare that is not one of the constants");
        return h;
    }
    PipelineSlot fresh{};
    std::vector<gs_ngfx_blob_entry> entries;
    if (!read_blob(shader, "pipeline", &fresh.code, &entries)) return h;
    const gs_ngfx_blob_entry* vs = find_entry(entries, GS_NGFX_BLOB_STAGE_VERTEX, entry_name(vertex), "pipeline", "vertex");
    const gs_ngfx_blob_entry* fs = find_entry(entries, GS_NGFX_BLOB_STAGE_FRAGMENT, entry_name(fragment), "pipeline", "fragment");
    if (!vs || !fs) return h;
    fresh.vertex = vs->name;
    fresh.fragment = fs->name;
    fresh.desc = d;
    PipelineSlot* p = g.pipelines.add(h.id);
    if (p) *p = std::move(fresh);
    return h;
}

void gs_ngfx_release_pipeline(gs_ngfx_pipeline handle)
{
    if (!ngfx_need_device("release_pipeline")) return;
    PipelineSlot* p = g.pipelines.get(handle.id);
    if (!p) return;
    if (g.pipeline == handle.id) g.pipeline = 0;
    for (const PipelineVariant& v : p->variants)
        defer_release([](void* pso) { destroy_pso(static_cast<PSO*>(pso)); }, v.pso);
    g.pipelines.remove(handle.id);
}

gs_ngfx_compute_pipeline gs_ngfx_create_compute_pipeline(gs_ngfx_bytes shader, gs_ngfx_bytes entry)
{
    gs_ngfx_compute_pipeline h{0};
    if (!ngfx_need_device("compute_pipeline")) return h;
    std::vector<uint32> code;
    std::vector<gs_ngfx_blob_entry> entries;
    if (!read_blob(shader, "compute_pipeline", &code, &entries)) return h;
    const gs_ngfx_blob_entry* cs = find_entry(entries, GS_NGFX_BLOB_STAGE_COMPUTE, entry_name(entry), "compute_pipeline", "compute");
    if (!cs) return h;
    // Metal needs the numthreads the blob recorded.
    PSO* pso = create_compute_pso(g.device, {.code = ByteSpan(code.data(), code.size() * 4), .entry_point = cs->name,
                                             .threadgroup_size = {.x = cs->threadgroup[0], .y = cs->threadgroup[1],
                                                                  .z = cs->threadgroup[2]}});
    if (!pso)
    {
        ngfx_fail("creating a compute pipeline for %s: %s", cs->name, ngfx_error_text());
        return h;
    }
    ComputeSlot* c = g.computes.add(h.id);
    if (c) c->pso = pso;
    return h;
}

gs_ngfx_compute_pipeline gs_ngfx_mip_pipeline(void)
{
    if (!ngfx_need_device("generate_mips")) return {0};
    if (g.computes.find(g.mip_pipeline.id)) return g.mip_pipeline;
    // ngfx_mips.slang's numthreads.
    constexpr uint32x3 mip_threadgroup = {.x = 8, .y = 8, .z = 1};
    const std::vector<uint32> words = aligned_words(ngfx_mips_code, sizeof ngfx_mips_code);
    PSO* pso = create_compute_pso(g.device, {.code = ByteSpan(words.data(), sizeof ngfx_mips_code), .entry_point = "downsample",
                                             .threadgroup_size = mip_threadgroup});
    if (!pso)
    {
        ngfx_fail("creating generate_mips' pipeline: %s", ngfx_error_text());
        return {0};
    }
    ComputeSlot* c = g.computes.add(g.mip_pipeline.id);
    if (c) c->pso = pso;
    return g.mip_pipeline;
}

void gs_ngfx_release_compute_pipeline(gs_ngfx_compute_pipeline handle)
{
    if (!ngfx_need_device("release_compute_pipeline")) return;
    ComputeSlot* c = g.computes.get(handle.id);
    if (!c) return;
    if (g.compute == handle.id) g.compute = 0;
    defer_release([](void* pso) { destroy_pso(static_cast<PSO*>(pso)); }, c->pso);
    g.computes.remove(handle.id);
}

uint8_t gs_ngfx_begin_pass(const gs_ngfx_pass_desc* desc)
{
    if (!ngfx_need_device("begin_pass")) return 0;
    if (g.in_pass) return ngfx_misuse("begin_pass inside a pass: end_pass first");
    const gs_ngfx_pass_desc& d = *desc;
    ColorAttachment colors[4];
    Targets t{};
    uint32 width = 0, height = 0;
    for (uint32 i = 0; i < 4 && d.color[i].id; ++i)
    {
        TextureSlot* s = texture_slot(d.color[i]);
        if (!s) return 0;
        if (!has(s->desc.usage, TextureUsage::color_attachment))
            return ngfx_misuse("begin_pass: a color target made without COLOR_TARGET usage");
        RenderView* view = render_view(s, uint32(d.mip), uint32(d.layer));
        if (!view) return ngfx_fail("begin_pass: creating a render view");
        colors[i] = {.render_view = view, .load = d.clear_color ? LoadOp::clear : LoadOp::load,
                     .clear = {.x = d.color_value.x, .y = d.color_value.y, .z = d.color_value.z, .w = d.color_value.w}};
        t.color[i] = s->desc.format;
        t.count = i + 1;
        width = s->desc.extent.x;
        height = s->desc.extent.y;
    }
    DepthAttachment depth{};
    StencilAttachment stencil{};
    if (d.depth.id)
    {
        TextureSlot* s = texture_slot(d.depth);
        if (!s) return 0;
        if (!has(s->desc.usage, TextureUsage::depth_stencil_attachment))
            return ngfx_misuse("begin_pass: a depth target made without DEPTH_TARGET usage");
        RenderView* view = render_view(s, 0, 0);
        if (!view) return ngfx_fail("begin_pass: creating a render view");
        const LoadOp load = d.clear_depth ? LoadOp::clear : LoadOp::load;
        depth = {.render_view = view, .load = load, .clear = d.depth_value};
        t.depth = s->desc.format;
        if (get_texture_format_info(s->desc.format).stencil)
        {
            stencil = {.render_view = view, .load = load};
            t.stencil = s->desc.format;
        }
        if (t.count && (s->desc.extent.x != width || s->desc.extent.y != height))
            return ngfx_misuse("begin_pass: a depth target of %u x %u for color targets of %u x %u", s->desc.extent.x,
                               s->desc.extent.y, width, height);
    }
    if (!t.count && !d.depth.id) return ngfx_misuse("begin_pass: no targets");
    CommandBuffer* commands = frame_commands();
    if (g.full_sync) full_sync_barrier(commands);
    // The barriers whose two sides the module sees: the targets, named here
    // and in earlier passes. What shaders read is the program's to order.
    uint32 targets[5];
    uint32 target_count = 0;
    for (uint32 i = 0; i < t.count; ++i) targets[target_count++] = d.color[i].id;
    if (d.depth.id) targets[target_count++] = d.depth.id;
    bool reused = false;
    for (uint32 i = 0; i < target_count; ++i)
        for (uint32 w : g.written_targets) reused = reused || w == targets[i];
    if (!g.frame_barrier)
    {
        // The frame boundary, once a frame before its first pass: the last
        // frame's target writes, and its screen copy's read, before this
        // frame writes targets. It covers examples/cube's depth barrier.
        frame_boundary_barrier(commands);
        g.frame_barrier = true;
        g.written_targets.clear();
    }
    else if (reused)
    {
        // A target an earlier pass drew into since the last of these: its
        // writes before this pass's (the same scopes, writes only).
        barrier(commands, Stage::color_output | Stage::depth_stencil_tests, Access::color_write | Access::depth_stencil_write,
                Stage::color_output | Stage::depth_stencil_tests, Access::color_write | Access::depth_stencil_write);
        g.written_targets.clear();
    }
    for (uint32 i = 0; i < target_count; ++i) g.written_targets.push_back(targets[i]);
    begin_render_pass(commands, {.colors = {colors, t.count}, .depth = depth, .stencil = stencil});
    g.in_pass = true;
    g.pass_targets = t;
    if (!t.count)
    {
        TextureSlot* s = texture_slot(d.depth);
        width = s->desc.extent.x;
        height = s->desc.extent.y;
    }
    g.pass_width = width >> d.mip ? width >> d.mip : 1;
    g.pass_height = height >> d.mip ? height >> d.mip : 1;
    g.bound_pso = nullptr;
    return 1;
}

void gs_ngfx_end_pass(void)
{
    if (!ngfx_need_device("end_pass")) return;
    if (!g.in_pass)
    {
        ngfx_misuse("end_pass with no pass begun");
        return;
    }
    end_render_pass(g.commands);
    if (g.full_sync) full_sync_barrier(g.commands);
    g.in_pass = false;
    g.bound_pso = nullptr;
}

void gs_ngfx_bind_pipeline(gs_ngfx_pipeline p)
{
    if (!ngfx_need_device("bind")) return;
    if (!g.in_pass)
    {
        ngfx_misuse("bind(pipeline) outside a pass: begin_pass first");
        return;
    }
    if (g.pipelines.get(p.id) && p.id != g.pipeline)
    {
        g.pipeline = p.id;
        g.bound_pso = nullptr;
    }
}

void gs_ngfx_bind_compute_pipeline(gs_ngfx_compute_pipeline p)
{
    if (!ngfx_need_device("bind")) return;
    if (g.in_pass)
    {
        ngfx_misuse("bind(compute pipeline) inside a pass: dispatches go between passes");
        return;
    }
    if (g.computes.get(p.id)) g.compute = p.id;
}

void gs_ngfx_viewport(float x, float y, float w, float h)
{
    if (!ngfx_need_device("viewport")) return;
    if (!g.in_pass)
    {
        ngfx_misuse("viewport outside a pass");
        return;
    }
    set_viewport(g.commands, {.x = x, .y = y, .width = w, .height = h});
}

void gs_ngfx_scissor(int64_t x, int64_t y, int64_t w, int64_t h)
{
    if (!ngfx_need_device("scissor")) return;
    if (!g.in_pass)
    {
        ngfx_misuse("scissor outside a pass");
        return;
    }
    if (w == 0)
    {
        // The whole target: the pass's first color target (or its depth
        // target) at the pass's mip.
        x = y = 0;
        w = g.pass_width;
        h = g.pass_height;
    }
    if (x < 0 || y < 0 || w < 0 || h < 0)
    {
        ngfx_misuse("scissor %lld,%lld + %lld x %lld", (long long)x, (long long)y, (long long)w, (long long)h);
        return;
    }
    set_scissor(g.commands, {.x = int32(x), .y = int32(y), .width = uint32(w), .height = uint32(h)});
}

void gs_ngfx_draw(uint64_t root, int64_t vertices, int64_t instances, int64_t first_vertex, int64_t first_instance)
{
    if (!ngfx_need_device("draw") || !bind_for_draw("draw")) return;
    if (root % 16) ngfx_misuse("draw: a root at %#llx is not 16-byte aligned", (unsigned long long)root);
    if (vertices <= 0 || instances <= 0) return;
    draw(g.commands, reinterpret_cast<const void*>(uintptr_t(root)), uint32(vertices), uint32(instances),
         uint32(first_vertex), uint32(first_instance));
}

void gs_ngfx_draw_indexed(uint64_t root, gs_ngfx_buffer indices, int64_t index_size, int64_t count, int64_t instances,
                          int64_t first_index, int64_t vertex_offset, int64_t first_instance)
{
    if (!ngfx_need_device("draw_indexed") || !bind_for_draw("draw_indexed")) return;
    if (root % 16) ngfx_misuse("draw_indexed: a root at %#llx is not 16-byte aligned", (unsigned long long)root);
    BufferSlot* b = g.buffers.get(indices.id);
    if (!b) return;
    if (index_size != 2 && index_size != 4)
    {
        ngfx_misuse("draw_indexed: indices of %lld bytes, not 2 or 4", (long long)index_size);
        return;
    }
    if (count <= 0 || instances <= 0) return;
    if (uint64(first_index + count) * uint64(index_size) > b->range.size)
    {
        ngfx_misuse("draw_indexed: %lld indices from %lld in a buffer of %llu bytes", (long long)count,
                    (long long)first_index, (unsigned long long)b->range.size);
        return;
    }
    draw_indexed(g.commands, reinterpret_cast<const void*>(uintptr_t(root)), b->range,
                 index_size == 2 ? IndexType::uint16 : IndexType::uint32, uint32(count), uint32(instances),
                 uint32(first_index), int32(vertex_offset), uint32(first_instance));
}

void gs_ngfx_dispatch(uint64_t root, int64_t x, int64_t y, int64_t z)
{
    if (!ngfx_need_device("dispatch")) return;
    if (g.in_pass)
    {
        ngfx_misuse("dispatch inside a pass: dispatches go between passes");
        return;
    }
    ComputeSlot* c = g.compute ? g.computes.get(g.compute) : nullptr;
    if (!c)
    {
        if (!g.compute) ngfx_misuse("dispatch with no compute pipeline bound");
        return;
    }
    if (root % 16) ngfx_misuse("dispatch: a root at %#llx is not 16-byte aligned", (unsigned long long)root);
    if (x <= 0 || y <= 0 || z <= 0) return;
    if (g.full_sync) full_sync_barrier(g.commands);
    if (g.bound_pso != c->pso)
    {
        bind_pso(g.commands, c->pso);
        g.bound_pso = c->pso;
    }
    dispatch(g.commands, reinterpret_cast<const void*>(uintptr_t(root)), {.x = uint32(x), .y = uint32(y), .z = uint32(z)});
}

void gs_ngfx_barrier(int32_t before, int32_t after)
{
    if (!ngfx_need_device("barrier")) return;
    if (g.in_pass)
    {
        ngfx_misuse("barrier inside a pass: barriers go between passes and dispatches");
        return;
    }
    // A point is a Stage mask in bits 0-11 and an Access mask in bits 16-27.
    // host is the module's own (read-back), never a program's.
    constexpr uint32 stages = 0xfffu & ~uint32(Stage::host);
    constexpr uint32 accesses = 0xfffu & ~uint32(Access::host_read);
    for (int32_t point : {before, after})
    {
        const uint32 p = uint32(point);
        if ((p & ~(stages | (accesses << 16))) || !(p & stages))
        {
            ngfx_misuse("barrier(%d, %d): each side is STAGE_* | ACCESS_* constants, with at least one stage", before,
                        after);
            return;
        }
    }
    barrier(g.commands, Stage(uint32(before) & 0xfff), Access((uint32(before) >> 16) & 0xfff), Stage(uint32(after) & 0xfff),
            Access((uint32(after) >> 16) & 0xfff));
}

} // extern "C"
