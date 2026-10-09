// The ngfx layer's device and frames: opening NoGraphicsAPI, recording and
// submitting each frame, the upload queue and deferred releases, and the
// screen, which is copied into the window's image when a frame ends.

#include "ngfx_gpu.hpp"


namespace ngfx {

State g;

namespace {

void report_error(const char* message, bool fatal) noexcept
{
    ngfx_fail("NoGraphicsAPI: %s", message);
    if (fatal) fprintf(stderr, "ngfx: NoGraphicsAPI: %s\n", message);
}

// Something to release once the submission that last used it has finished.
void run_pending(uint64 retire)
{
    for (size_t i = 0; i < g.pending_fns.size(); ++i)
    {
        void (*fn)(void*) = g.pending_fns[i];
        void* arg = g.pending_args[i];
        g.deletes->defer(retire, [fn, arg]() noexcept { fn(arg); });
    }
    g.pending_fns.clear();
    g.pending_args.clear();
}

void begin_frame_commands()
{
    g.commands = begin_commands(g.pools[g.slot]);
    // Command-buffer bindings are not inherited (NoGraphicsAPI.hpp), so every
    // buffer gets the heaps.
    set_texture_descriptor_heap(g.commands, g.texture_descriptors);
    set_sampler_descriptor_heap(g.commands, g.sampler_descriptors);
    g.bound_pso = nullptr;
}

// Clears the screen to black and far, as gfx's starts.
void clear_screen()
{
    TextureSlot* color = texture_slot(g.screen);
    TextureSlot* depth = texture_slot(g.screen_depth);
    if (!color || !depth) return;
    CommandBuffer* commands = frame_commands();
    frame_boundary_barrier(commands);
    const ColorAttachment colors[] = {{.render_view = render_view(color, 0, 0), .load = LoadOp::clear,
                                       .clear = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f}}};
    begin_render_pass(commands, {.colors = colors,
                                 .depth = {.render_view = render_view(depth, 0, 0), .load = LoadOp::clear, .clear = 1.0f}});
    end_render_pass(commands);
}

bool create_screen(int w, int h)
{
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    const gs_ngfx_texture_desc color{.kind = GS_NGFX_TEXTURE_2D, .format = GS_NGFX_RGBA8,
                                     .usage = GS_NGFX_SAMPLED | GS_NGFX_COLOR_TARGET, .width = w, .height = h,
                                     .depth = 1, .mips = 1};
    const gs_ngfx_texture_desc depth{.kind = GS_NGFX_TEXTURE_2D, .format = GS_NGFX_DEPTH,
                                     .usage = GS_NGFX_DEPTH_TARGET, .width = w, .height = h, .depth = 1, .mips = 1};
    // The handles stay; the slots get new textures, the old ones released
    // once the frames using them finish.
    const gs_ngfx_texture_desc* descs[] = {&color, &depth};
    gs_ngfx_texture* handles[] = {&g.screen, &g.screen_depth};
    for (int i = 0; i < 2; ++i)
    {
        uint32 id = 0;
        TextureSlot* fresh = nullptr;
        if (!create_texture(*descs[i], &id, &fresh)) return false;
        TextureSlot* old = g.textures.find(handles[i]->id);
        if (old)
        {
            release_texture_later(*old);
            *old = *fresh;
            g.textures.remove(id);
        }
        else
        {
            handles[i]->id = id;
        }
    }
    g.width = w;
    g.height = h;
    clear_screen();
    return true;
}

// Draws the screen into the window's image: a fullscreen triangle sampling
// it, nearest when the sizes match, as gfx's blit filters.
void copy_screen(const SwapchainFrame& frame)
{
    TextureSlot* screen = texture_slot(g.screen);
    // examples/deferred_renderer's barrier before its lighting pass samples
    // what the frame drew.
    barrier(g.commands, Stage::color_output | Stage::depth_stencil_tests, Access::color_write | Access::depth_stencil_write,
            Stage::fragment, Access::shader_read);
    const ColorAttachment colors[] = {{.render_view = frame.render_view, .load = LoadOp::discard}};
    begin_render_pass(g.commands, {.colors = colors});
    bind_pso(g.commands, g.copy_pso);
    g.bound_pso = g.copy_pso;
    const bool same = frame.extent.x == uint32(g.width) && frame.extent.y == uint32(g.height);
    const uint32 root_values[4] = {screen->sampled, g.copy_samplers[same ? 0 : 1], 0, 0};
    const GpuCpuRange<byte> root = g.frame_data[g.slot]->allocate(sizeof root_values);
    memcpy(root.cpu, root_values, sizeof root_values);
    draw(g.commands, root.gpu, 3);
    end_render_pass(g.commands);
}

} // namespace

CommandBuffer* frame_commands() { return g.commands; }

CommandBuffer* begin_setup() { return begin_commands(g.pools[g.slot]); }

void end_setup(CommandBuffer* commands)
{
    end_commands(commands);
    ++g.timeline.value;
    submit(g.device, {.commands = {commands}, .completion = g.timeline});
    g.pool_values[g.slot] = g.timeline.value;
}

void defer_release(void (*fn)(void*), void* arg)
{
    g.pending_fns.push_back(fn);
    g.pending_args.push_back(arg);
}

void flush_uploads()
{
    g.uploads->flush();
    g.uploaded.clear();
}

void upload(GpuRange destination, const void* data, uint64 size)
{
    const byte* begin = static_cast<const byte*>(destination.gpu);
    for (const GpuRange& r : g.uploaded)
    {
        const byte* other = static_cast<const byte*>(r.gpu);
        if (begin < other + r.size && other < begin + size)
        {
            flush_uploads();
            break;
        }
    }
    g.uploads->upload_buffer({.gpu = destination.gpu, .size = size}, ByteSpan(data, size_t(size)));
    g.uploaded.push_back({.gpu = destination.gpu, .size = size});
}

void submit_frame_commands(bool present)
{
    // The uploads made so far go first, on the same queue: UploadQueue's own
    // barriers order them against the work before and after them.
    flush_uploads();
    end_commands(g.commands);
    ++g.timeline.value;
    if (present) submit_and_present(g.device, {.commands = {g.commands}, .completion = g.timeline});
    else submit(g.device, {.commands = {g.commands}, .completion = g.timeline});
    g.pool_values[g.slot] = g.timeline.value;
    g.commands = nullptr;
    run_pending(g.timeline.value);
}

void finish()
{
    submit_frame_commands(false);
    wait_timeline(g.timeline);
    g.deletes->tick();
    begin_frame_commands();
}

} // namespace ngfx

using namespace ngfx;

extern "C" {

bool ngfx_gpu_in_pass(void) { return g.in_pass; }

const char* ngfx_gpu_driver(void)
{
#if defined(__APPLE__)
    return "metal";
#else
    return "vulkan";
#endif
}

void ngfx_gpu_screen_size(int* w, int* h)
{
    *w = g.width;
    *h = g.height;
}

bool ngfx_gpu_open(void* layer, int w, int h)
{
    set_error_callback(report_error);
    g.windowed = layer != nullptr;
    const char* sync = getenv("GOOSE_NGFX_SYNC");
    g.full_sync = sync && !strcmp(sync, "full");
    // The screen is copied as it is, so the window's image has its format,
    // without sRGB conversion, as gfx's swapchain is.
    const DeviceInit init = create_device({.window = layer, .swapchain_format = layer ? Format::bgra8_unorm : Format::undefined});
    if (!init.device)
    {
        char why[512];
        snprintf(why, sizeof why, "%s", ngfx_error_text());
        ngfx_fail("no NoGraphicsAPI device (error %d)%s%s", int(init.error), why[0] ? ": " : "", why);
        return false;
    }
    g.device = init.device;
    g.timeline.semaphore = create_timeline_semaphore(g.device);
    g.frame_heap = create_gpu_heap(g.device, frame_memory_size * frames_in_flight, MemoryType::cpu_visible);
    g.texture_heap = create_texture_heap(g.device, texture_heap_size);
    g.texture_descriptors = create_texture_descriptor_heap(g.device, texture_descriptors);
    g.sampler_descriptors = create_sampler_descriptor_heap(g.device, sampler_descriptors);
    if (!g.timeline.semaphore || !g.frame_heap.owner || !g.texture_heap.owner || !g.texture_descriptors ||
        !g.sampler_descriptors)
    {
        if (!ngfx_error_text()[0]) ngfx_fail("creating the device's resources");
        return false;
    }
    for (uint32 i = 0; i < frames_in_flight; ++i)
    {
        g.pools[i] = create_command_pool(g.device);
        if (!g.pools[i]) return ngfx_fail("creating a command pool");
        GpuCpuRange<byte> part{.cpu = g.frame_heap.range.cpu + i * frame_memory_size,
                               .gpu = g.frame_heap.range.gpu + i * frame_memory_size, .size = frame_memory_size};
        g.frame_data[i] = new BumpAllocator(part);
    }
    g.uploads = new UploadQueue(g.device, upload_capacity, 0, frames_in_flight);
    if (!g.uploads->valid()) return ngfx_fail("creating the upload queue");
    g.deletes = new DeleteQueue(g.timeline.semaphore, delete_capacity);
    g.texture_allocator = new TextureAllocator(g.device, g.texture_heap, max_textures);
    g.texture_slots.capacity = texture_descriptors;
    g.sampler_slots.capacity = sampler_descriptors;
    for (int i = 0; i < 2; ++i)
    {
        g.copy_samplers[i] = g.sampler_slots.allocate();
        const Filter f = i ? Filter::linear : Filter::nearest;
        write_sampler_descriptor(g.sampler_descriptors, g.copy_samplers[i],
                                 {.min_filter = f, .mag_filter = f, .mip_filter = Filter::nearest,
                                  .address_u = AddressMode::clamp_to_edge, .address_v = AddressMode::clamp_to_edge,
                                  .address_w = AddressMode::clamp_to_edge});
    }
    if (g.windowed && !(g.copy_pso = create_copy_pso())) return false;
    begin_frame_commands();
    return create_screen(w, h);
}

void ngfx_gpu_close(void)
{
    if (!g.device)
    {
        g = State{};
        return;
    }
    if (g.commands)
    {
        if (g.in_pass) end_render_pass(g.commands);
        g.in_pass = false;
        submit_frame_commands(false);
    }
    wait_idle(g.device);
    run_pending(g.timeline.value);
    if (g.uploads) g.uploads->destroy();
    delete g.uploads;
    if (g.deletes) g.deletes->drain();
    delete g.deletes;
    release_pipelines();
    if (g.copy_pso) destroy_pso(g.copy_pso);
    g.textures.each([](TextureSlot& s) { free_texture(s); });
    g.buffers.each([](BufferSlot& s) {
        if (s.chunk < 0) destroy_gpu_heap(s.own);
    });
    for (HeapAllocator* a : g.buffer_allocators) delete a;
    for (const GpuHeap& h : g.buffer_heaps) destroy_gpu_heap(h);
    delete g.texture_allocator;
    for (uint32 i = 0; i < frames_in_flight; ++i)
    {
        delete g.frame_data[i];
        destroy_command_pool(g.pools[i]);
    }
    destroy_gpu_heap(g.frame_heap);
    destroy_texture_heap(g.texture_heap);
    destroy_texture_descriptor_heap(g.texture_descriptors);
    destroy_sampler_descriptor_heap(g.sampler_descriptors);
    destroy_timeline_semaphore(g.timeline.semaphore);
    destroy_device(g.device);
    set_error_callback(nullptr);
    g = State{};
}

void ngfx_gpu_next_frame(bool present)
{
    // The window's image comes last: acquired outside a pass, written by the
    // copy, and presented by the submission that acquired it.
    bool presenting = false;
    if (present && g.windowed)
    {
        const SwapchainFrame frame = acquire(g.commands);
        if (frame.render_view)
        {
            copy_screen(frame);
            presenting = true;
        }
    }
    submit_frame_commands(presenting);
    // The next frame in flight's pool and memory, once the frame that last
    // used them has finished, as examples/cube waits.
    g.slot = (g.slot + 1) % frames_in_flight;
    if (g.pool_values[g.slot]) wait_timeline({.semaphore = g.timeline.semaphore, .value = g.pool_values[g.slot]});
    reset_command_pool(g.pools[g.slot]);
    g.frame_data[g.slot]->reset();
    g.deletes->tick();
    g.uploads->reclaim();
    begin_frame_commands();
    g.frame_barrier = false;
    g.written_targets.clear();
}

bool ngfx_gpu_resize(int w, int h) { return create_screen(w, h); }

gs_ngfx_texture gs_ngfx_screen(void)
{
    ngfx_need_device("screen");
    return g.screen;
}

gs_ngfx_texture gs_ngfx_screen_depth(void)
{
    ngfx_need_device("screen_depth");
    return g.screen_depth;
}

void ngfx_gpu_finish(void) { finish(); }

} // extern "C"
