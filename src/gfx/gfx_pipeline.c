/* The gfx layer: shaders from the blobs embed_shader produces, graphics
   pipelines, and compute pipelines. */

#include "gfx_internal.h"

#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#pragma warning(push)
#pragma warning(disable: 4115)
#include <d3dcompiler.h>
#pragma warning(pop)
#endif

/* --- DXBC ------------------------------------------------------------------ */
/* SDL_GPU's D3D12 backend takes DXBC. The blob carries HLSL, which the
   system's own FXC, d3dcompiler_47.dll (part of Windows 10 and 11), turns
   into DXBC: loaded by name, so no SDK import library is needed. */

#ifdef _WIN32
typedef HRESULT(WINAPI *gfx_d3dcompile_fn)(LPCVOID src, SIZE_T size, LPCSTR name,
                                           const D3D_SHADER_MACRO *defines, ID3DInclude *include,
                                           LPCSTR entry, LPCSTR target, UINT flags1, UINT flags2,
                                           ID3DBlob **code, ID3DBlob **errors);
#endif

bool gfx_have_dxbc(void) {
#ifdef _WIN32
    if (!gfx.d3dcompile) {
        if (!gfx.d3dcompiler) gfx.d3dcompiler = (void *)LoadLibraryW(L"d3dcompiler_47.dll");
        if (gfx.d3dcompiler)
            gfx.d3dcompile = (void *)GetProcAddress((HMODULE)gfx.d3dcompiler, "D3DCompile");
    }
    return gfx.d3dcompile != NULL;
#else
    return false;
#endif
}

#ifdef _WIN32
static bool gfx_compile_hlsl(const char *hlsl, size_t len, int stage, void **code, size_t *size) {
    static const char *targets[] = { "vs_5_1", "ps_5_1", "cs_5_1" };
    ID3DBlob *out = NULL, *errors = NULL;
    HRESULT hr = ((gfx_d3dcompile_fn)gfx.d3dcompile)(hlsl, len, "embedded shader", NULL, NULL,
                                                     "main", targets[stage], 0, 0, &out, &errors);
    if (FAILED(hr)) {
        const char *msg = errors ? (const char *)ID3D10Blob_GetBufferPointer(errors) : "";
        gfx_misuse("FXC could not compile the HLSL for a %s shader: %s", targets[stage], msg);
        if (errors) ID3D10Blob_Release(errors);
        if (out) ID3D10Blob_Release(out);
        return false;
    }
    *size = ID3D10Blob_GetBufferSize(out);
    *code = malloc(*size);
    memcpy(*code, ID3D10Blob_GetBufferPointer(out), *size);
    ID3D10Blob_Release(out);
    if (errors) ID3D10Blob_Release(errors);
    return true;
}
#endif

/* --- blobs ----------------------------------------------------------------- */

bool gfx_read_blob(gs_gfx_bytes blob, gs_gfx_blob_header *h, gfx_shader_info *info) {
    if (blob.len >= (int64_t)sizeof *h) memcpy(h, blob.data, sizeof *h);
    if (blob.len < (int64_t)sizeof *h || memcmp(h->magic, GS_GFX_BLOB_MAGIC, 4) != 0)
        return gfx_misuse("not a shader: pass what embed_shader returns");
    if (h->version != GS_GFX_BLOB_VERSION)
        return gfx_misuse("a shader blob of version %d, where this gfx reads version %d",
                          h->version, GS_GFX_BLOB_VERSION);
    uint64_t len = (uint64_t)blob.len;
    if (h->stage > GS_GFX_STAGE_COMPUTE || h->num_uniform_buffers > 4 || h->num_inputs > 16 ||
        (uint64_t)h->spirv_offset + h->spirv_size > len ||
        (uint64_t)h->msl_offset + h->msl_size >= len ||
        (uint64_t)h->hlsl_offset + h->hlsl_size >= len)
        return gfx_misuse("a damaged shader blob");
    memset(info, 0, sizeof *info);
    info->stage = h->stage;
    info->num_samplers = h->num_samplers;
    info->num_storage_textures_ro = h->num_storage_textures_ro;
    info->num_storage_textures_rw = h->num_storage_textures_rw;
    info->num_storage_buffers_ro = h->num_storage_buffers_ro;
    info->num_storage_buffers_rw = h->num_storage_buffers_rw;
    info->num_uniform_buffers = h->num_uniform_buffers;
    memcpy(info->local_size, h->local_size, sizeof info->local_size);
    const uint8_t *p = blob.data + sizeof *h;
    for (int i = 0; i < h->num_uniform_buffers; i++, p += 4) memcpy(&info->uniform_sizes[i], p, 4);
    info->num_inputs = h->num_inputs;
    for (int i = 0; i < h->num_inputs; i++, p += sizeof(gs_gfx_blob_input))
        memcpy(&info->inputs[i], p, sizeof(gs_gfx_blob_input));
    return true;
}

bool gfx_shader_code(gs_gfx_bytes blob, const gs_gfx_blob_header *h, void **code, size_t *size,
                     const char **entry) {
    *entry = "main";
    if (gfx.format == SDL_GPU_SHADERFORMAT_SPIRV) {
        /* A copy, since the words must be 4-byte aligned and nothing aligns
           the array the blob lives in. */
        *size = h->spirv_size;
        *code = malloc(*size);
        memcpy(*code, blob.data + h->spirv_offset, *size);
        return true;
    }
    if (gfx.format == SDL_GPU_SHADERFORMAT_MSL) {
        *entry = "main0";
        *size = h->msl_size;
        *code = malloc(*size + 1);
        memcpy(*code, blob.data + h->msl_offset, *size + 1);
        return true;
    }
#ifdef _WIN32
    if (gfx.format == SDL_GPU_SHADERFORMAT_DXBC)
        return gfx_compile_hlsl((const char *)blob.data + h->hlsl_offset, h->hlsl_size, h->stage,
                                code, size);
#endif
    return gfx_misuse("no shader format this device takes");
}

static uint64_t gfx_hash(const uint8_t *p, int64_t n) {
    uint64_t hash = 14695981039346656037ull;
    for (int64_t i = 0; i < n; i++) hash = (hash ^ p[i]) * 1099511628211ull;
    return hash;
}

gfx_shader *gfx_get_shader(gs_gfx_bytes blob, int stage) {
    gs_gfx_blob_header h;
    gfx_shader_info info;
    if (!gfx_read_blob(blob, &h, &info)) return NULL;
    static const char *names[] = { "vertex", "fragment", "compute" };
    if (h.stage != stage) {
        gfx_misuse("a %s shader where a %s shader goes", names[h.stage], names[stage]);
        return NULL;
    }
    uint64_t hash = gfx_hash(blob.data, blob.len);
    for (int i = 0; i < gfx.nshaders; i++)
        if (gfx.shaders[i]->hash == hash && gfx.shaders[i]->size == (uint32_t)blob.len)
            return gfx.shaders[i];
    void *code;
    size_t size;
    const char *entry;
    if (!gfx_shader_code(blob, &h, &code, &size, &entry)) return NULL;
    SDL_GPUShaderCreateInfo ci;
    SDL_zero(ci);
    ci.code = (const Uint8 *)code;
    ci.code_size = size;
    ci.entrypoint = entry;
    ci.format = gfx.format;
    ci.stage = stage == GS_GFX_STAGE_VERTEX ? SDL_GPU_SHADERSTAGE_VERTEX
                                            : SDL_GPU_SHADERSTAGE_FRAGMENT;
    ci.num_samplers = info.num_samplers;
    ci.num_storage_textures = info.num_storage_textures_ro;
    ci.num_storage_buffers = info.num_storage_buffers_ro;
    ci.num_uniform_buffers = info.num_uniform_buffers;
    SDL_GPUShader *shader = SDL_CreateGPUShader(gfx.dev, &ci);
    free(code);
    if (!shader) {
        gfx_misuse("creating a %s shader: %s", names[stage], SDL_GetError());
        return NULL;
    }
    if (gfx.nshaders == gfx.shaders_cap) {
        gfx.shaders_cap = gfx.shaders_cap ? gfx.shaders_cap * 2 : 16;
        gfx.shaders = (gfx_shader **)realloc(gfx.shaders, sizeof(gfx_shader *) * (size_t)gfx.shaders_cap);
    }
    gfx_shader *s = (gfx_shader *)calloc(1, sizeof *s);
    s->hash = hash;
    s->size = (uint32_t)blob.len;
    s->shader = shader;
    s->info = info;
    gfx.shaders[gfx.nshaders++] = s;
    return s;
}

/* --- vertex layout ------------------------------------------------------- */

typedef struct {
    SDL_GPUVertexElementFormat format;
    uint32_t size;
    char kind;      /* 'f' float, 'i' signed, 'u' unsigned: what the shader input must be */
} gfx_vertex_format;

/* By the VERTEX_* constant. */
static const gfx_vertex_format gfx_vertex_formats[] = {
    { SDL_GPU_VERTEXELEMENTFORMAT_INVALID, 0, 0 },
    { SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, 4, 'f' },
    { SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 8, 'f' },
    { SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 12, 'f' },
    { SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 16, 'f' },
    { SDL_GPU_VERTEXELEMENTFORMAT_INT, 4, 'i' },
    { SDL_GPU_VERTEXELEMENTFORMAT_INT2, 8, 'i' },
    { SDL_GPU_VERTEXELEMENTFORMAT_INT3, 12, 'i' },
    { SDL_GPU_VERTEXELEMENTFORMAT_INT4, 16, 'i' },
    { SDL_GPU_VERTEXELEMENTFORMAT_UINT, 4, 'u' },
    { SDL_GPU_VERTEXELEMENTFORMAT_UINT2, 8, 'u' },
    { SDL_GPU_VERTEXELEMENTFORMAT_UINT3, 12, 'u' },
    { SDL_GPU_VERTEXELEMENTFORMAT_UINT4, 16, 'u' },
    { SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 4, 'f' },
    { SDL_GPU_VERTEXELEMENTFORMAT_BYTE4_NORM, 4, 'f' },
    { SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4, 4, 'u' },
    { SDL_GPU_VERTEXELEMENTFORMAT_SHORT2, 4, 'i' },
    { SDL_GPU_VERTEXELEMENTFORMAT_SHORT4, 8, 'i' },
    { SDL_GPU_VERTEXELEMENTFORMAT_USHORT2_NORM, 4, 'f' },
    { SDL_GPU_VERTEXELEMENTFORMAT_USHORT4_NORM, 8, 'f' },
    { SDL_GPU_VERTEXELEMENTFORMAT_HALF2, 4, 'f' },
    { SDL_GPU_VERTEXELEMENTFORMAT_HALF4, 8, 'f' },
};

/* The VERTEX_* format a shader input of this type reads by default. */
static int gfx_input_format(uint8_t type) {
    switch (type) {
        case GS_GFX_INPUT_FLOAT: return GS_GFX_VERTEX_FLOAT;
        case GS_GFX_INPUT_FLOAT2: return GS_GFX_VERTEX_FLOAT2;
        case GS_GFX_INPUT_FLOAT3: return GS_GFX_VERTEX_FLOAT3;
        case GS_GFX_INPUT_FLOAT4: return GS_GFX_VERTEX_FLOAT4;
        case GS_GFX_INPUT_INT: return GS_GFX_VERTEX_INT;
        case GS_GFX_INPUT_INT2: return GS_GFX_VERTEX_INT2;
        case GS_GFX_INPUT_INT3: return GS_GFX_VERTEX_INT3;
        case GS_GFX_INPUT_INT4: return GS_GFX_VERTEX_INT4;
        case GS_GFX_INPUT_UINT: return GS_GFX_VERTEX_UINT;
        case GS_GFX_INPUT_UINT2: return GS_GFX_VERTEX_UINT2;
        case GS_GFX_INPUT_UINT3: return GS_GFX_VERTEX_UINT3;
        case GS_GFX_INPUT_UINT4: return GS_GFX_VERTEX_UINT4;
        default: return 0;
    }
}

static char gfx_input_kind(uint8_t type) {
    switch (type) {
        case GS_GFX_INPUT_INT: case GS_GFX_INPUT_INT2: case GS_GFX_INPUT_INT3:
        case GS_GFX_INPUT_INT4:
            return 'i';
        case GS_GFX_INPUT_UINT: case GS_GFX_INPUT_UINT2: case GS_GFX_INPUT_UINT3:
        case GS_GFX_INPUT_UINT4:
            return 'u';
        default:
            return 'f';
    }
}

/* The vertex buffers a pipeline reads, from the vertex shader's inputs:
   each input in location order, packed one after the other the way a
   packed Goose struct lays out its fields, in buffer 0, or per instance in
   buffer 1 from desc->instance_location on. */
static bool gfx_vertex_layout(gfx_pipeline_slot *p) {
    gs_gfx_blob_input in[16];
    int n = p->vs->info.num_inputs;
    memcpy(in, p->vs->info.inputs, sizeof(in[0]) * (size_t)n);
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && in[j - 1].location > in[j].location; j--) {
            gs_gfx_blob_input t = in[j];
            in[j] = in[j - 1];
            in[j - 1] = t;
        }
    uint32_t offset[2] = { 0, 0 };
    for (int i = 0; i < n; i++) {
        int loc = in[i].location;
        int slot = p->desc.instance_location >= 0 && loc >= p->desc.instance_location ? 1 : 0;
        int columns = in[i].type == GS_GFX_INPUT_MAT4 ? 4 : 1;
        int fmt = columns == 4 ? GS_GFX_VERTEX_FLOAT4 : gfx_input_format(in[i].type);
        int override = p->desc.formats[loc];
        if (override) {
            if (columns == 4 || override > GS_GFX_VERTEX_HALF4 ||
                gfx_vertex_formats[override].kind != gfx_input_kind(in[i].type))
                return gfx_misuse("PipelineDesc.formats[%d]: format %d cannot feed the vertex "
                                  "shader's input at location %d", loc, override, loc);
            fmt = override;
        }
        for (int c = 0; c < columns; c++) {
            SDL_GPUVertexAttribute *a = &p->attrs[p->nattrs++];
            a->location = (Uint32)(loc + c);
            a->buffer_slot = (Uint32)slot;
            a->format = gfx_vertex_formats[fmt].format;
            a->offset = offset[slot];
            offset[slot] += gfx_vertex_formats[fmt].size;
        }
    }
    for (int slot = 0; slot < 2; slot++) {
        if (!offset[slot]) continue;
        SDL_GPUVertexBufferDescription *vb = &p->vbufs[p->nvbufs++];
        vb->slot = (Uint32)slot;
        vb->pitch = offset[slot];
        vb->input_rate = slot ? SDL_GPU_VERTEXINPUTRATE_INSTANCE : SDL_GPU_VERTEXINPUTRATE_VERTEX;
        p->vbuf_mask |= 1u << slot;
    }
    return true;
}

/* --- graphics pipelines ------------------------------------------------------ */

static SDL_GPUColorTargetBlendState gfx_blend(int32_t mode) {
    SDL_GPUColorTargetBlendState b;
    SDL_zero(b);
    b.color_blend_op = SDL_GPU_BLENDOP_ADD;
    b.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    b.enable_blend = mode != GS_GFX_BLEND_NONE;
    switch (mode) {
        case GS_GFX_BLEND_ALPHA:
            b.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            b.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            b.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            b.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            break;
        case GS_GFX_BLEND_ADD:
            b.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            b.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            b.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            b.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            break;
        case GS_GFX_BLEND_PREMULTIPLIED:
            b.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            b.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            b.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            b.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            break;
        case GS_GFX_BLEND_MULTIPLY:
            b.src_color_blendfactor = SDL_GPU_BLENDFACTOR_DST_COLOR;
            b.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            b.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
            b.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
            break;
        default:
            break;
    }
    return b;
}

SDL_GPUGraphicsPipeline *gfx_pipeline_for(gfx_pipeline_slot *p, const gfx_targets *t) {
    for (int i = 0; i < p->nvariants; i++)
        if (!memcmp(&p->variants[i].targets, t, sizeof *t)) return p->variants[i].p;
    if (p->nvariants == GFX_MAX_VARIANTS) {
        gfx_misuse("a pipeline drawn into more than %d different combinations of target formats",
                   GFX_MAX_VARIANTS);
        return NULL;
    }
    const gs_gfx_pipeline_desc *d = &p->desc;
    SDL_GPUGraphicsPipelineCreateInfo ci;
    SDL_zero(ci);
    ci.vertex_shader = p->vs->shader;
    ci.fragment_shader = p->fs->shader;
    ci.vertex_input_state.vertex_buffer_descriptions = p->vbufs;
    ci.vertex_input_state.num_vertex_buffers = (Uint32)p->nvbufs;
    ci.vertex_input_state.vertex_attributes = p->attrs;
    ci.vertex_input_state.num_vertex_attributes = (Uint32)p->nattrs;
    ci.primitive_type = (SDL_GPUPrimitiveType)d->primitive;
    ci.rasterizer_state.fill_mode = d->wireframe ? SDL_GPU_FILLMODE_LINE : SDL_GPU_FILLMODE_FILL;
    ci.rasterizer_state.cull_mode = (SDL_GPUCullMode)d->cull;
    ci.rasterizer_state.front_face = d->clockwise ? SDL_GPU_FRONTFACE_CLOCKWISE
                                                  : SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    ci.rasterizer_state.depth_bias_constant_factor = d->depth_bias;
    ci.rasterizer_state.depth_bias_slope_factor = d->depth_bias_slope;
    ci.rasterizer_state.enable_depth_bias = d->depth_bias != 0.0f || d->depth_bias_slope != 0.0f;
    ci.rasterizer_state.enable_depth_clip = true;
    ci.multisample_state.sample_count = t->samples;
    ci.depth_stencil_state.compare_op = d->depth_compare ? (SDL_GPUCompareOp)d->depth_compare
                                                         : SDL_GPU_COMPAREOP_LESS;
    ci.depth_stencil_state.enable_depth_test = d->depth_test && t->has_depth;
    ci.depth_stencil_state.enable_depth_write = d->depth_write && t->has_depth;
    SDL_GPUColorTargetDescription colors[4];
    for (int i = 0; i < t->ncolor; i++) {
        SDL_zero(colors[i]);
        colors[i].format = t->color[i];
        colors[i].blend_state = gfx_blend(d->blend);
    }
    ci.target_info.color_target_descriptions = colors;
    ci.target_info.num_color_targets = (Uint32)t->ncolor;
    ci.target_info.depth_stencil_format = t->depth;
    ci.target_info.has_depth_stencil_target = t->has_depth;
    SDL_GPUGraphicsPipeline *gp = SDL_CreateGPUGraphicsPipeline(gfx.dev, &ci);
    if (!gp) {
        gfx_misuse("creating a pipeline: %s", SDL_GetError());
        return NULL;
    }
    gfx_variant *v = &p->variants[p->nvariants++];
    v->targets = *t;
    v->p = gp;
    return gp;
}

gs_gfx_pipeline gs_gfx_create_pipeline(gs_gfx_bytes vs, gs_gfx_bytes fs,
                                       const gs_gfx_pipeline_desc *desc) {
    gs_gfx_pipeline h = { 0 };
    if (!gfx_need_device("pipeline")) return h;
    const gs_gfx_pipeline_desc *d = desc;
    if ((uint32_t)d->primitive > 4 || (uint32_t)d->cull > 2 || (uint32_t)d->blend > 4 ||
        (uint32_t)d->depth_compare > 8 || d->instance_location > 15) {
        gfx_misuse("a PipelineDesc with a primitive, cull, blend or compare that is not one of "
                   "gfx's constants");
        return h;
    }
    gfx_shader *v = gfx_get_shader(vs, GS_GFX_STAGE_VERTEX);
    gfx_shader *f = v ? gfx_get_shader(fs, GS_GFX_STAGE_FRAGMENT) : NULL;
    if (!f) return h;
    gfx_pipeline_slot *p = (gfx_pipeline_slot *)gfx_table_add(&gfx.pipelines, &h.id);
    if (!p) return h;
    p->vs = v;
    p->fs = f;
    p->desc = *d;
    /* The SDL pipeline itself waits for the pass it is bound in, whose
       targets it has to match: guessing the screen's here would fail for a
       shader writing integers to an R32UI target. */
    if (!gfx_vertex_layout(p)) {
        gfx_table_remove(&gfx.pipelines, h.id);
        h.id = 0;
    }
    return h;
}

static void gfx_free_pipeline(void *item) {
    gfx_pipeline_slot *p = (gfx_pipeline_slot *)item;
    for (int i = 0; i < p->nvariants; i++) SDL_ReleaseGPUGraphicsPipeline(gfx.dev, p->variants[i].p);
    p->nvariants = 0;
}

void gs_gfx_release_pipeline(gs_gfx_pipeline p) {
    if (!gfx_need_device("release_pipeline")) return;
    gfx_pipeline_slot *s = (gfx_pipeline_slot *)gfx_table_get(&gfx.pipelines, p.id);
    if (!s) return;
    if (gfx.pipeline == s) gfx.pipeline = NULL;
    gfx_free_pipeline(s);
    gfx_table_remove(&gfx.pipelines, p.id);
}

/* --- compute pipelines ------------------------------------------------------- */

gs_gfx_compute_pipeline gs_gfx_create_compute_pipeline(gs_gfx_bytes cs) {
    gs_gfx_compute_pipeline h = { 0 };
    if (!gfx_need_device("compute_pipeline")) return h;
    gs_gfx_blob_header bh;
    gfx_shader_info info;
    if (!gfx_read_blob(cs, &bh, &info)) return h;
    if (bh.stage != GS_GFX_STAGE_COMPUTE) {
        gfx_misuse("a %s shader where a compute shader goes",
                   bh.stage == GS_GFX_STAGE_VERTEX ? "vertex" : "fragment");
        return h;
    }
    void *code;
    size_t size;
    const char *entry;
    if (!gfx_shader_code(cs, &bh, &code, &size, &entry)) return h;
    SDL_GPUComputePipelineCreateInfo ci;
    SDL_zero(ci);
    ci.code = (const Uint8 *)code;
    ci.code_size = size;
    ci.entrypoint = entry;
    ci.format = gfx.format;
    ci.num_samplers = info.num_samplers;
    ci.num_readonly_storage_textures = info.num_storage_textures_ro;
    ci.num_readonly_storage_buffers = info.num_storage_buffers_ro;
    ci.num_readwrite_storage_textures = info.num_storage_textures_rw;
    ci.num_readwrite_storage_buffers = info.num_storage_buffers_rw;
    ci.num_uniform_buffers = info.num_uniform_buffers;
    ci.threadcount_x = info.local_size[0] ? info.local_size[0] : 1;
    ci.threadcount_y = info.local_size[1] ? info.local_size[1] : 1;
    ci.threadcount_z = info.local_size[2] ? info.local_size[2] : 1;
    SDL_GPUComputePipeline *cp = SDL_CreateGPUComputePipeline(gfx.dev, &ci);
    free(code);
    if (!cp) {
        gfx_misuse("creating a compute pipeline: %s", SDL_GetError());
        return h;
    }
    gfx_compute_slot *s = (gfx_compute_slot *)gfx_table_add(&gfx.computes, &h.id);
    if (!s) {
        SDL_ReleaseGPUComputePipeline(gfx.dev, cp);
        return h;
    }
    s->p = cp;
    s->info = info;
    return h;
}

static void gfx_free_compute(void *item) {
    SDL_ReleaseGPUComputePipeline(gfx.dev, ((gfx_compute_slot *)item)->p);
}

void gs_gfx_release_compute_pipeline(gs_gfx_compute_pipeline p) {
    if (!gfx_need_device("release_compute_pipeline")) return;
    gfx_compute_slot *s = (gfx_compute_slot *)gfx_table_get(&gfx.computes, p.id);
    if (!s) return;
    if (gfx.compute == s) gfx.compute = NULL;
    gfx_free_compute(s);
    gfx_table_remove(&gfx.computes, p.id);
}

void gfx_release_pipelines(void) {
    gfx_table_each(&gfx.pipelines, gfx_free_pipeline);
    gfx_table_each(&gfx.computes, gfx_free_compute);
    gfx_table_clear(&gfx.pipelines);
    gfx_table_clear(&gfx.computes);
    for (int i = 0; i < gfx.nshaders; i++) {
        SDL_ReleaseGPUShader(gfx.dev, gfx.shaders[i]->shader);
        free(gfx.shaders[i]);
    }
    free(gfx.shaders);
    gfx.shaders = NULL;
    gfx.nshaders = gfx.shaders_cap = 0;
}
