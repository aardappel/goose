/* The gfx layer: render and compute passes, binding, uniforms, draws and
   dispatches. Every draw and dispatch first checks that what the bound
   pipeline's shaders read has been bound, which SDL_GPU leaves to the
   backend to crash on. */

#include "gfx_internal.h"

static void gfx_reset_bindings(void) {
    memset(gfx.samplers_bound, 0, sizeof gfx.samplers_bound);
    memset(gfx.textures_bound, 0, sizeof gfx.textures_bound);
    memset(gfx.buffers_bound, 0, sizeof gfx.buffers_bound);
    gfx.vbufs_bound = 0;
    gfx.index_bound = false;
}

/* --- render passes ----------------------------------------------------------- */

uint8_t gs_gfx_begin_pass(const gs_gfx_pass_desc *d) {
    if (!gfx_need_device("begin_pass")) return 0;
    if (gfx.pass || gfx.cpass)
        return gfx_misuse("begin_pass inside a %s pass: end it first",
                          gfx.pass ? "render" : "compute");
    SDL_GPUColorTargetInfo ct[4];
    gfx_targets t;
    SDL_zero(t);
    t.samples = SDL_GPU_SAMPLECOUNT_1;
    int32_t samples = 0;
    uint32_t w = 0, h = 0;
    for (int i = 0; i < 4 && d->color[i].id; i++) {
        gfx_texture_slot *s = gfx_texture_slot_of(d->color[i]);
        if (!s) return 0;
        if (!(s->usage & SDL_GPU_TEXTUREUSAGE_COLOR_TARGET))
            return gfx_misuse("begin_pass: color target %d was not created with COLOR_TARGET", i);
        int32_t layers = s->desc.kind == GS_GFX_TEXTURE_CUBE ? 6
                       : s->desc.kind == GS_GFX_TEXTURE_2D ? 1 : s->desc.depth;
        if (d->mip < 0 || d->mip >= s->desc.mips || d->layer < 0 || d->layer >= layers)
            return gfx_misuse("begin_pass: color target %d has no mip %d, layer %d", i, d->mip,
                              d->layer);
        uint32_t tw = (uint32_t)s->desc.width >> d->mip, th = (uint32_t)s->desc.height >> d->mip;
        tw = tw ? tw : 1;
        th = th ? th : 1;
        if (i && (tw != w || th != h || s->desc.samples != samples))
            return gfx_misuse("begin_pass: the color targets differ in size or sample count");
        w = tw;
        h = th;
        samples = s->desc.samples;
        SDL_zero(ct[i]);
        ct[i].texture = s->tex;
        ct[i].mip_level = (Uint32)d->mip;
        ct[i].layer_or_depth_plane = (Uint32)d->layer;
        ct[i].clear_color.r = d->color_value.x;
        ct[i].clear_color.g = d->color_value.y;
        ct[i].clear_color.b = d->color_value.z;
        ct[i].clear_color.a = d->color_value.w;
        ct[i].load_op = d->clear_color ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
        ct[i].store_op = SDL_GPU_STOREOP_STORE;
        /* Cleared anyway, so while an earlier frame still reads the old
           contents, SDL may give it fresh memory rather than wait. */
        ct[i].cycle = d->clear_color != 0;
        if (d->resolve[i].id) {
            gfx_texture_slot *r = gfx_texture_slot_of(d->resolve[i]);
            if (!r) return 0;
            if (s->desc.samples < 2 || r->desc.samples != 1 || r->format != s->format ||
                r->desc.width != s->desc.width || r->desc.height != s->desc.height)
                return gfx_misuse("begin_pass: resolve target %d must be a single-sample texture "
                                  "of the multisampled target's size and format", i);
            ct[i].resolve_texture = r->tex;
            ct[i].store_op = SDL_GPU_STOREOP_RESOLVE_AND_STORE;
        }
        t.color[i] = s->format;
        t.ncolor = i + 1;
    }
    SDL_GPUDepthStencilTargetInfo dt;
    SDL_zero(dt);
    if (d->depth.id) {
        gfx_texture_slot *s = gfx_texture_slot_of(d->depth);
        if (!s) return 0;
        if (!(s->usage & SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
            return gfx_misuse("begin_pass: the depth target was not created with DEPTH_TARGET");
        if (t.ncolor && ((uint32_t)s->desc.width != w || (uint32_t)s->desc.height != h ||
                         s->desc.samples != samples))
            return gfx_misuse("begin_pass: the depth target is %d x %d with %d sample(s), the "
                              "color targets %u x %u with %d", s->desc.width, s->desc.height,
                              s->desc.samples, w, h, samples);
        w = (uint32_t)s->desc.width;
        h = (uint32_t)s->desc.height;
        samples = s->desc.samples;
        dt.texture = s->tex;
        dt.clear_depth = d->depth_value;
        dt.load_op = d->clear_depth ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
        dt.store_op = SDL_GPU_STOREOP_STORE;
        dt.stencil_load_op = SDL_GPU_LOADOP_CLEAR;
        dt.stencil_store_op = SDL_GPU_STOREOP_STORE;
        dt.cycle = d->clear_depth != 0;
        t.has_depth = true;
        t.depth = s->format;
    }
    if (!t.ncolor && !t.has_depth) return gfx_misuse("begin_pass with no targets");
    t.samples = samples == 8 ? SDL_GPU_SAMPLECOUNT_8 : samples == 4 ? SDL_GPU_SAMPLECOUNT_4
              : samples == 2 ? SDL_GPU_SAMPLECOUNT_2 : SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUCommandBuffer *cb = gfx_cmd();
    if (!cb) return 0;
    gfx.pass = SDL_BeginGPURenderPass(cb, ct, (Uint32)t.ncolor, t.has_depth ? &dt : NULL);
    if (!gfx.pass) return gfx_sdl_fail("beginning a render pass");
    gfx.targets = t;
    gfx.pipeline = NULL;
    gfx_reset_bindings();
    SDL_GPUViewport vp = { 0, 0, (float)w, (float)h, 0, 1 };
    SDL_SetGPUViewport(gfx.pass, &vp);
    SDL_Rect sc = { 0, 0, (int)w, (int)h };
    SDL_SetGPUScissor(gfx.pass, &sc);
    return 1;
}

void gs_gfx_end_pass(void) {
    if (!gfx.pass) {
        gfx_misuse("end_pass with no render pass begun");
        return;
    }
    SDL_EndGPURenderPass(gfx.pass);
    gfx.pass = NULL;
    gfx.pipeline = NULL;
}

void gs_gfx_bind_pipeline(gs_gfx_pipeline p) {
    if (!gfx.pass) {
        gfx_misuse("bind_pipeline outside a render pass");
        return;
    }
    gfx_pipeline_slot *s = (gfx_pipeline_slot *)gfx_table_get(&gfx.pipelines, p.id);
    if (!s) return;
    if ((s->desc.depth_test || s->desc.depth_write) && !gfx.targets.has_depth) {
        gfx_misuse("bind_pipeline: the pipeline tests or writes depth, and the pass has no "
                   "depth target");
        return;
    }
    SDL_GPUGraphicsPipeline *gp = gfx_pipeline_for(s, &gfx.targets);
    if (!gp) return;
    SDL_BindGPUGraphicsPipeline(gfx.pass, gp);
    gfx.pipeline = s;
}

void gs_gfx_bind_vertex_buffer(int64_t slot, gs_gfx_buffer b, int64_t offset) {
    if (!gfx.pass) {
        gfx_misuse("bind_vertex_buffer outside a render pass");
        return;
    }
    gfx_buffer_slot *s = gfx_buffer_slot_of(b);
    if (!s) return;
    if (slot < 0 || slot > 1 || offset < 0 || offset >= s->size ||
        !(s->usage & SDL_GPU_BUFFERUSAGE_VERTEX)) {
        gfx_misuse("bind_vertex_buffer: slot %lld (0, or 1 for instances), offset %lld, of a "
                   "BUFFER_VERTEX buffer", (long long)slot, (long long)offset);
        return;
    }
    SDL_GPUBufferBinding bb = { s->buf, (Uint32)offset };
    SDL_BindGPUVertexBuffers(gfx.pass, (Uint32)slot, &bb, 1);
    gfx.vbufs_bound |= 1u << slot;
}

void gs_gfx_bind_index_buffer(gs_gfx_buffer b, int64_t index_size, int64_t offset) {
    if (!gfx.pass) {
        gfx_misuse("bind_index_buffer outside a render pass");
        return;
    }
    gfx_buffer_slot *s = gfx_buffer_slot_of(b);
    if (!s) return;
    if ((index_size != 2 && index_size != 4) || offset < 0 || offset >= s->size ||
        !(s->usage & SDL_GPU_BUFFERUSAGE_INDEX)) {
        gfx_misuse("bind_index_buffer: indices of 2 or 4 bytes, not %lld, from a BUFFER_INDEX "
                   "buffer", (long long)index_size);
        return;
    }
    SDL_GPUBufferBinding bb = { s->buf, (Uint32)offset };
    SDL_BindGPUIndexBuffer(gfx.pass, &bb, index_size == 2 ? SDL_GPU_INDEXELEMENTSIZE_16BIT
                                                          : SDL_GPU_INDEXELEMENTSIZE_32BIT);
    gfx.index_bound = true;
}

/* The pass a stage's bindings go to; a misuse if it is not begun. */
static bool gfx_stage_pass(int64_t stage, int64_t slot, const char *what) {
    if (stage < GS_GFX_VERTEX || stage > GS_GFX_COMPUTE)
        return gfx_misuse("%s: stage %lld is not VERTEX, FRAGMENT or COMPUTE", what,
                          (long long)stage);
    if (slot < 0 || slot > 31) return gfx_misuse("%s: slot %lld", what, (long long)slot);
    if (stage == GS_GFX_COMPUTE ? !gfx.cpass : !gfx.pass)
        return gfx_misuse("%s for the %s stage outside a %s pass", what, gfx_stage_name(stage),
                          stage == GS_GFX_COMPUTE ? "compute" : "render");
    return true;
}

void gs_gfx_bind_texture(int64_t stage, int64_t slot, gs_gfx_texture t, gs_gfx_sampler smp) {
    if (!gfx_stage_pass(stage, slot, "bind_texture")) return;
    gfx_texture_slot *s = gfx_texture_slot_of(t);
    gfx_sampler_slot *ss = (gfx_sampler_slot *)gfx_table_get(&gfx.samplers, smp.id);
    if (!s || !ss) return;
    if (!(s->usage & SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        gfx_misuse("bind_texture: the texture was not created SAMPLED");
        return;
    }
    SDL_GPUTextureSamplerBinding b = { s->tex, ss->smp };
    if (stage == GS_GFX_VERTEX) SDL_BindGPUVertexSamplers(gfx.pass, (Uint32)slot, &b, 1);
    else if (stage == GS_GFX_FRAGMENT) SDL_BindGPUFragmentSamplers(gfx.pass, (Uint32)slot, &b, 1);
    else SDL_BindGPUComputeSamplers(gfx.cpass, (Uint32)slot, &b, 1);
    gfx.samplers_bound[stage] |= 1u << slot;
}

void gs_gfx_bind_storage_texture(int64_t stage, int64_t slot, gs_gfx_texture t) {
    if (!gfx_stage_pass(stage, slot, "bind_storage_texture")) return;
    gfx_texture_slot *s = gfx_texture_slot_of(t);
    if (!s) return;
    uint32_t need = stage == GS_GFX_COMPUTE ? SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ
                                            : SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ;
    if (!(s->usage & need)) {
        gfx_misuse("bind_storage_texture: the texture was not created %s",
                   stage == GS_GFX_COMPUTE ? "COMPUTE_READ" : "STORAGE_READ");
        return;
    }
    if (stage == GS_GFX_VERTEX) SDL_BindGPUVertexStorageTextures(gfx.pass, (Uint32)slot, &s->tex, 1);
    else if (stage == GS_GFX_FRAGMENT)
        SDL_BindGPUFragmentStorageTextures(gfx.pass, (Uint32)slot, &s->tex, 1);
    else SDL_BindGPUComputeStorageTextures(gfx.cpass, (Uint32)slot, &s->tex, 1);
    gfx.textures_bound[stage] |= 1u << slot;
}

void gs_gfx_bind_storage_buffer(int64_t stage, int64_t slot, gs_gfx_buffer b) {
    if (!gfx_stage_pass(stage, slot, "bind_storage_buffer")) return;
    gfx_buffer_slot *s = gfx_buffer_slot_of(b);
    if (!s) return;
    uint32_t need = stage == GS_GFX_COMPUTE ? SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ
                                            : SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    if (!(s->usage & need)) {
        gfx_misuse("bind_storage_buffer: the buffer was not created %s",
                   stage == GS_GFX_COMPUTE ? "BUFFER_COMPUTE_READ" : "BUFFER_STORAGE");
        return;
    }
    if (stage == GS_GFX_VERTEX) SDL_BindGPUVertexStorageBuffers(gfx.pass, (Uint32)slot, &s->buf, 1);
    else if (stage == GS_GFX_FRAGMENT)
        SDL_BindGPUFragmentStorageBuffers(gfx.pass, (Uint32)slot, &s->buf, 1);
    else SDL_BindGPUComputeStorageBuffers(gfx.cpass, (Uint32)slot, &s->buf, 1);
    gfx.buffers_bound[stage] |= 1u << slot;
}

/* What a stage of the bound pipeline declares, or NULL with none bound. */
static const gfx_shader_info *gfx_bound_info(int64_t stage) {
    if (stage == GS_GFX_COMPUTE) return gfx.compute ? &gfx.compute->info : NULL;
    if (!gfx.pipeline) return NULL;
    return stage == GS_GFX_VERTEX ? &gfx.pipeline->vs->info : &gfx.pipeline->fs->info;
}

void gs_gfx_push_uniforms(int64_t stage, int64_t slot, gs_gfx_bytes data) {
    if (!gfx_need_device("push_uniforms")) return;
    if (stage < GS_GFX_VERTEX || stage > GS_GFX_COMPUTE || slot < 0 || slot > 3) {
        gfx_misuse("push_uniforms: stage %lld, slot %lld (0 to 3)", (long long)stage,
                   (long long)slot);
        return;
    }
    if (data.len <= 0 || data.len > 32768) {
        gfx_misuse("push_uniforms: %lld bytes (1 to 32768)", (long long)data.len);
        return;
    }
    /* The check v1 makes against the shader: the size. A Goose struct is
       packed, where a uniform block is laid out by std140, which aligns a
       vec3 or vec4 to 16 bytes; `pad` fields make up the difference. */
    const gfx_shader_info *info = gfx_bound_info(stage);
    if (info && slot < info->num_uniform_buffers && info->uniform_sizes[slot] != data.len) {
        gfx_misuse("push_uniforms: the %s shader's uniform block %lld is %u bytes by std140, "
                   "and %lld were pushed", gfx_stage_name(stage), (long long)slot,
                   info->uniform_sizes[slot], (long long)data.len);
        return;
    }
    SDL_GPUCommandBuffer *cb = gfx_cmd();
    if (!cb) return;
    if (stage == GS_GFX_VERTEX)
        SDL_PushGPUVertexUniformData(cb, (Uint32)slot, data.data, (Uint32)data.len);
    else if (stage == GS_GFX_FRAGMENT)
        SDL_PushGPUFragmentUniformData(cb, (Uint32)slot, data.data, (Uint32)data.len);
    else
        SDL_PushGPUComputeUniformData(cb, (Uint32)slot, data.data, (Uint32)data.len);
    gfx.uniforms_pushed[stage] |= 1u << slot;
}

void gs_gfx_viewport(float x, float y, float w, float h) {
    if (!gfx.pass) {
        gfx_misuse("viewport outside a render pass");
        return;
    }
    SDL_GPUViewport vp = { x, y, w, h, 0, 1 };
    SDL_SetGPUViewport(gfx.pass, &vp);
}

void gs_gfx_scissor(int64_t x, int64_t y, int64_t w, int64_t h) {
    if (!gfx.pass) {
        gfx_misuse("scissor outside a render pass");
        return;
    }
    SDL_Rect r = { (int)x, (int)y, (int)w, (int)h };
    SDL_SetGPUScissor(gfx.pass, &r);
}

/* Checks that the shaders of `stage` get everything they read. */
static bool gfx_stage_ready(int64_t stage, const gfx_shader_info *info, const char *what) {
    struct {
        uint32_t count, bound;
        const char *kind, *how;
    } need[] = {
        { info->num_samplers, gfx.samplers_bound[stage], "sampler", "bind_texture" },
        { info->num_storage_textures_ro, gfx.textures_bound[stage], "storage texture",
          "bind_storage_texture" },
        { info->num_storage_buffers_ro, gfx.buffers_bound[stage], "storage buffer",
          "bind_storage_buffer" },
        { info->num_uniform_buffers, gfx.uniforms_pushed[stage], "uniform block",
          "push_uniforms" },
    };
    for (int i = 0; i < 4; i++)
        for (uint32_t slot = 0; slot < need[i].count; slot++)
            if (!(need[i].bound & (1u << slot)))
                return gfx_misuse("%s: the %s shader reads %s %u, which no %s gave it", what,
                                  gfx_stage_name(stage), need[i].kind, slot, need[i].how);
    return true;
}

static bool gfx_ready_to_draw(const char *what) {
    if (!gfx.pass) return gfx_misuse("%s outside a render pass", what);
    gfx_pipeline_slot *p = gfx.pipeline;
    if (!p) return gfx_misuse("%s with no pipeline bound", what);
    uint32_t missing = p->vbuf_mask & ~gfx.vbufs_bound;
    if (missing)
        return gfx_misuse("%s: the pipeline reads vertex buffer %d, which is not bound", what,
                          missing & 1 ? 0 : 1);
    return gfx_stage_ready(GS_GFX_VERTEX, &p->vs->info, what) &&
           gfx_stage_ready(GS_GFX_FRAGMENT, &p->fs->info, what);
}

void gs_gfx_draw(int64_t vertices, int64_t instances, int64_t first_vertex,
                 int64_t first_instance) {
    if (!gfx_ready_to_draw("draw")) return;
    if (vertices < 0 || instances < 0 || first_vertex < 0 || first_instance < 0) {
        gfx_misuse("draw: a negative count");
        return;
    }
    SDL_DrawGPUPrimitives(gfx.pass, (Uint32)vertices, (Uint32)instances, (Uint32)first_vertex,
                          (Uint32)first_instance);
}

void gs_gfx_draw_indexed(int64_t indices, int64_t instances, int64_t first_index,
                         int64_t vertex_offset, int64_t first_instance) {
    if (!gfx_ready_to_draw("draw_indexed")) return;
    if (!gfx.index_bound) {
        gfx_misuse("draw_indexed with no index buffer bound");
        return;
    }
    if (indices < 0 || instances < 0 || first_index < 0 || first_instance < 0) {
        gfx_misuse("draw_indexed: a negative count");
        return;
    }
    SDL_DrawGPUIndexedPrimitives(gfx.pass, (Uint32)indices, (Uint32)instances,
                                 (Uint32)first_index, (Sint32)vertex_offset,
                                 (Uint32)first_instance);
}

/* --- compute ----------------------------------------------------------------- */

uint8_t gs_gfx_begin_compute(gs_gfx_buffer_slice rw_buffers, gs_gfx_texture_slice rw_textures) {
    if (!gfx_need_device("begin_compute")) return 0;
    if (gfx.pass || gfx.cpass)
        return gfx_misuse("begin_compute inside a %s pass: end it first",
                          gfx.pass ? "render" : "compute");
    if (rw_buffers.len > 8 || rw_textures.len > 8)
        return gfx_misuse("begin_compute: at most 8 read-write buffers and 8 textures");
    SDL_GPUStorageBufferReadWriteBinding bb[8];
    SDL_GPUStorageTextureReadWriteBinding tb[8];
    for (int64_t i = 0; i < rw_buffers.len; i++) {
        gfx_buffer_slot *s = gfx_buffer_slot_of(rw_buffers.data[i]);
        if (!s) return 0;
        if (!(s->usage & SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE))
            return gfx_misuse("begin_compute: read-write buffer %lld was not created "
                              "BUFFER_COMPUTE_WRITE", (long long)i);
        SDL_zero(bb[i]);
        bb[i].buffer = s->buf;
    }
    for (int64_t i = 0; i < rw_textures.len; i++) {
        gfx_texture_slot *s = gfx_texture_slot_of(rw_textures.data[i]);
        if (!s) return 0;
        if (!(s->usage & (SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE |
                          SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_SIMULTANEOUS_READ_WRITE)))
            return gfx_misuse("begin_compute: read-write texture %lld was not created "
                              "COMPUTE_WRITE or COMPUTE_READ_WRITE", (long long)i);
        SDL_zero(tb[i]);
        tb[i].texture = s->tex;
    }
    SDL_GPUCommandBuffer *cb = gfx_cmd();
    if (!cb) return 0;
    gfx.cpass = SDL_BeginGPUComputePass(cb, tb, (Uint32)rw_textures.len, bb,
                                        (Uint32)rw_buffers.len);
    if (!gfx.cpass) return gfx_sdl_fail("beginning a compute pass");
    gfx.compute = NULL;
    gfx.compute_rw_buffers = (int)rw_buffers.len;
    gfx.compute_rw_textures = (int)rw_textures.len;
    gfx.samplers_bound[GS_GFX_COMPUTE] = gfx.textures_bound[GS_GFX_COMPUTE] = 0;
    gfx.buffers_bound[GS_GFX_COMPUTE] = 0;
    return 1;
}

void gs_gfx_end_compute(void) {
    if (!gfx.cpass) {
        gfx_misuse("end_compute with no compute pass begun");
        return;
    }
    SDL_EndGPUComputePass(gfx.cpass);
    gfx.cpass = NULL;
    gfx.compute = NULL;
}

void gs_gfx_bind_compute_pipeline(gs_gfx_compute_pipeline p) {
    if (!gfx.cpass) {
        gfx_misuse("bind_compute_pipeline outside a compute pass");
        return;
    }
    gfx_compute_slot *s = (gfx_compute_slot *)gfx_table_get(&gfx.computes, p.id);
    if (!s) return;
    if (s->info.num_storage_buffers_rw != gfx.compute_rw_buffers ||
        s->info.num_storage_textures_rw != gfx.compute_rw_textures) {
        gfx_misuse("bind_compute_pipeline: the shader writes %d buffer(s) and %d texture(s), "
                   "and begin_compute was given %d and %d", s->info.num_storage_buffers_rw,
                   s->info.num_storage_textures_rw, gfx.compute_rw_buffers,
                   gfx.compute_rw_textures);
        return;
    }
    SDL_BindGPUComputePipeline(gfx.cpass, s->p);
    gfx.compute = s;
}

void gs_gfx_dispatch(int64_t x, int64_t y, int64_t z) {
    if (!gfx.cpass) {
        gfx_misuse("dispatch outside a compute pass");
        return;
    }
    if (!gfx.compute) {
        gfx_misuse("dispatch with no compute pipeline bound");
        return;
    }
    if (x < 0 || y < 0 || z < 0) {
        gfx_misuse("dispatch: a negative group count");
        return;
    }
    if (!gfx_stage_ready(GS_GFX_COMPUTE, &gfx.compute->info, "dispatch")) return;
    SDL_DispatchGPUCompute(gfx.cpass, (Uint32)x, (Uint32)y, (Uint32)z);
}
