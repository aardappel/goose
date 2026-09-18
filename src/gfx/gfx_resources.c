/* The gfx layer: buffers, textures and samplers, and moving data between
   them and the program. */

#include "gfx_internal.h"

#include <stdlib.h>

/* --- formats --------------------------------------------------------------- */

bool gfx_texture_format(int32_t format, SDL_GPUTextureFormat *out) {
    switch (format) {
        case GS_GFX_RGBA8: *out = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM; return true;
        case GS_GFX_BGRA8: *out = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM; return true;
        case GS_GFX_RGBA8_SRGB: *out = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB; return true;
        case GS_GFX_R8: *out = SDL_GPU_TEXTUREFORMAT_R8_UNORM; return true;
        case GS_GFX_RG8: *out = SDL_GPU_TEXTUREFORMAT_R8G8_UNORM; return true;
        case GS_GFX_RGBA16F: *out = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT; return true;
        case GS_GFX_RGBA32F: *out = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT; return true;
        case GS_GFX_R16F: *out = SDL_GPU_TEXTUREFORMAT_R16_FLOAT; return true;
        case GS_GFX_RG16F: *out = SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT; return true;
        case GS_GFX_R32F: *out = SDL_GPU_TEXTUREFORMAT_R32_FLOAT; return true;
        case GS_GFX_RG32F: *out = SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT; return true;
        case GS_GFX_R32UI: *out = SDL_GPU_TEXTUREFORMAT_R32_UINT; return true;
        case GS_GFX_RGBA8UI: *out = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UINT; return true;
        case GS_GFX_RGB10A2: *out = SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM; return true;
        case GS_GFX_RG11B10F: *out = SDL_GPU_TEXTUREFORMAT_R11G11B10_UFLOAT; return true;
        case GS_GFX_DEPTH16: *out = SDL_GPU_TEXTUREFORMAT_D16_UNORM; return true;
        case GS_GFX_DEPTH24: *out = SDL_GPU_TEXTUREFORMAT_D24_UNORM; return true;
        case GS_GFX_DEPTH32F: *out = SDL_GPU_TEXTUREFORMAT_D32_FLOAT; return true;
        case GS_GFX_DEPTH24_STENCIL8: *out = SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT; return true;
        case GS_GFX_DEPTH:
            /* 32-bit float where it can be sampled too, as for a shadow map. */
            *out = gfx.dev && SDL_GPUTextureSupportsFormat(
                                  gfx.dev, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTURETYPE_2D,
                                  SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET |
                                      SDL_GPU_TEXTUREUSAGE_SAMPLER)
                       ? SDL_GPU_TEXTUREFORMAT_D32_FLOAT
                       : SDL_GPU_TEXTUREFORMAT_D24_UNORM;
            return true;
        default:
            return false;
    }
}

static bool gfx_is_depth(SDL_GPUTextureFormat f) {
    return f == SDL_GPU_TEXTUREFORMAT_D16_UNORM || f == SDL_GPU_TEXTUREFORMAT_D24_UNORM ||
           f == SDL_GPU_TEXTUREFORMAT_D32_FLOAT || f == SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT ||
           f == SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
}

const char *gfx_stage_name(int64_t stage) {
    return stage == GS_GFX_VERTEX ? "vertex" : stage == GS_GFX_FRAGMENT ? "fragment" : "compute";
}

gfx_texture_slot *gfx_texture_slot_of(gs_gfx_texture t) {
    return (gfx_texture_slot *)gfx_table_get(&gfx.textures, t.id);
}

gfx_buffer_slot *gfx_buffer_slot_of(gs_gfx_buffer b) {
    return (gfx_buffer_slot *)gfx_table_get(&gfx.buffers, b.id);
}

/* --- transfers ------------------------------------------------------------- */

/* A mapped upload or download buffer of `size` bytes. */
static SDL_GPUTransferBuffer *gfx_transfer(uint32_t size, bool upload) {
    SDL_GPUTransferBufferCreateInfo ci;
    SDL_zero(ci);
    ci.usage = upload ? SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD : SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    ci.size = size;
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(gfx.dev, &ci);
    if (!tb) gfx_sdl_fail("creating a transfer buffer");
    return tb;
}

/* Where a copy is recorded. A new resource's first contents go through a
   command buffer of their own, submitted at once: it can be created
   anywhere, even inside a pass, and nothing earlier can be using it. Any
   other copy goes into the command buffer being recorded, in program order
   with the draws around it, and so cannot happen inside a pass. */
static SDL_GPUCommandBuffer *gfx_copy_cmd(bool fresh, const char *what) {
    if (fresh) {
        SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer(gfx.dev);
        if (!cb) gfx_sdl_fail("acquiring a command buffer");
        return cb;
    }
    if (gfx.pass || gfx.cpass) {
        gfx_misuse("%s inside a %s pass: copies happen between passes", what,
                   gfx.pass ? "render" : "compute");
        return NULL;
    }
    return gfx_cmd();
}

static bool gfx_copy_done(SDL_GPUCommandBuffer *cb, bool fresh) {
    if (!fresh) return true;
    return SDL_SubmitGPUCommandBuffer(cb) || gfx_sdl_fail("submitting an upload");
}

/* Writes `len` bytes of `data` and then `zeros` zero bytes into `buf` at
   `offset`. `cycle` lets SDL give the buffer fresh memory when a draw still
   pending uses the old, for a rewrite of the whole buffer only. */
static bool gfx_upload_buffer(SDL_GPUBuffer *buf, uint32_t offset, const void *data,
                              uint32_t len, uint32_t zeros, bool fresh, bool cycle,
                              const char *what) {
    uint32_t total = len + zeros;
    if (!total) return true;
    SDL_GPUCommandBuffer *cb = gfx_copy_cmd(fresh, what);
    if (!cb) return false;
    SDL_GPUTransferBuffer *tb = gfx_transfer(total, true);
    if (!tb) {
        if (fresh) SDL_CancelGPUCommandBuffer(cb);
        return false;
    }
    uint8_t *p = (uint8_t *)SDL_MapGPUTransferBuffer(gfx.dev, tb, false);
    if (len) memcpy(p, data, len);
    if (zeros) memset(p + len, 0, zeros);
    SDL_UnmapGPUTransferBuffer(gfx.dev, tb);
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cb);
    SDL_GPUTransferBufferLocation src = { tb, 0 };
    SDL_GPUBufferRegion dst = { buf, offset, total };
    SDL_UploadToGPUBuffer(cp, &src, &dst, cycle);
    SDL_EndGPUCopyPass(cp);
    SDL_ReleaseGPUTransferBuffer(gfx.dev, tb);
    return gfx_copy_done(cb, fresh);
}

/* --- buffers --------------------------------------------------------------- */

gs_gfx_buffer gs_gfx_create_buffer(int64_t usage, int64_t size, gs_gfx_bytes data) {
    gs_gfx_buffer h = { 0 };
    if (!gfx_need_device("buffer")) return h;
    if (size < data.len) size = data.len;
    if (size <= 0 || size > 0x7fffffff) {
        gfx_misuse("a buffer of %lld bytes", (long long)size);
        return h;
    }
    if (!usage || (usage & ~(int64_t)63)) {
        gfx_misuse("buffer usage %lld is not a combination of the BUFFER_ flags", (long long)usage);
        return h;
    }
    SDL_GPUBufferCreateInfo ci;
    SDL_zero(ci);
    ci.usage = (SDL_GPUBufferUsageFlags)usage;
    ci.size = (Uint32)size;
    SDL_GPUBuffer *buf = SDL_CreateGPUBuffer(gfx.dev, &ci);
    if (!buf) {
        gfx_sdl_fail("creating a buffer");
        return h;
    }
    /* Zeroed past the data, rather than left as whatever the memory held. */
    if (!gfx_upload_buffer(buf, 0, data.data, (uint32_t)data.len, (uint32_t)(size - data.len),
                           true, false, "buffer")) {
        SDL_ReleaseGPUBuffer(gfx.dev, buf);
        return h;
    }
    gfx_buffer_slot *s = (gfx_buffer_slot *)gfx_table_add(&gfx.buffers, &h.id);
    if (!s) {
        SDL_ReleaseGPUBuffer(gfx.dev, buf);
        return h;
    }
    s->buf = buf;
    s->size = (uint32_t)size;
    s->usage = (uint32_t)usage;
    return h;
}

uint8_t gs_gfx_update_buffer(gs_gfx_buffer b, int64_t offset, gs_gfx_bytes data) {
    if (!gfx_need_device("update_buffer")) return 0;
    gfx_buffer_slot *s = gfx_buffer_slot_of(b);
    if (!s) return 0;
    if (offset < 0 || data.len < 0 || offset + data.len > (int64_t)s->size)
        return gfx_misuse("update_buffer: %lld bytes at %lld do not fit a buffer of %u",
                          (long long)data.len, (long long)offset, s->size);
    bool whole = offset == 0 && data.len == (int64_t)s->size;
    return gfx_upload_buffer(s->buf, (uint32_t)offset, data.data, (uint32_t)data.len, 0, false,
                             whole, "update_buffer");
}

/* Reads back through a download buffer: the copy is recorded after all the
   work so far, and this waits for the GPU to get through it. */
uint8_t gs_gfx_read_buffer(gs_gfx_buffer b, int64_t offset, gs_gfx_bytes out) {
    if (!gfx_need_device("read_buffer")) return 0;
    gfx_buffer_slot *s = gfx_buffer_slot_of(b);
    if (!s) return 0;
    if (offset < 0 || out.len < 0 || offset + out.len > (int64_t)s->size)
        return gfx_misuse("read_buffer: %lld bytes at %lld are not all in a buffer of %u",
                          (long long)out.len, (long long)offset, s->size);
    if (!out.len) return 1;
    SDL_GPUCommandBuffer *cb = gfx_copy_cmd(false, "read_buffer");
    if (!cb) return 0;
    SDL_GPUTransferBuffer *tb = gfx_transfer((uint32_t)out.len, false);
    if (!tb) return 0;
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cb);
    SDL_GPUBufferRegion src = { s->buf, (Uint32)offset, (Uint32)out.len };
    SDL_GPUTransferBufferLocation dst = { tb, 0 };
    SDL_DownloadFromGPUBuffer(cp, &src, &dst);
    SDL_EndGPUCopyPass(cp);
    bool ok = gfx_finish();
    if (ok) {
        void *p = SDL_MapGPUTransferBuffer(gfx.dev, tb, false);
        memcpy(out.data, p, (size_t)out.len);
        SDL_UnmapGPUTransferBuffer(gfx.dev, tb);
    }
    SDL_ReleaseGPUTransferBuffer(gfx.dev, tb);
    return ok;
}

int64_t gs_gfx_buffer_size(gs_gfx_buffer b) {
    gfx_buffer_slot *s = gfx_buffer_slot_of(b);
    return s ? s->size : 0;
}

void gs_gfx_release_buffer(gs_gfx_buffer b) {
    if (!gfx_need_device("release_buffer")) return;
    gfx_buffer_slot *s = gfx_buffer_slot_of(b);
    if (!s) return;
    SDL_ReleaseGPUBuffer(gfx.dev, s->buf);
    gfx_table_remove(&gfx.buffers, b.id);
}

/* --- textures -------------------------------------------------------------- */

static int32_t gfx_mip_size(int32_t size, int32_t mip) {
    size >>= mip;
    return size < 1 ? 1 : size;
}

static SDL_GPUSampleCount gfx_samples(int32_t n) {
    switch (n) {
        case 2: return SDL_GPU_SAMPLECOUNT_2;
        case 4: return SDL_GPU_SAMPLECOUNT_4;
        case 8: return SDL_GPU_SAMPLECOUNT_8;
        default: return SDL_GPU_SAMPLECOUNT_1;
    }
}

gs_gfx_texture gs_gfx_create_texture(const gs_gfx_texture_desc *desc) {
    gs_gfx_texture h = { 0 };
    if (!gfx_need_device("texture")) return h;
    gs_gfx_texture_desc d = *desc;
    SDL_GPUTextureFormat format;
    if (!gfx_texture_format(d.format, &format)) {
        gfx_misuse("texture format %d is not one of gfx's formats", d.format);
        return h;
    }
    if (d.kind < GS_GFX_TEXTURE_2D || d.kind > GS_GFX_TEXTURE_CUBE) {
        gfx_misuse("texture kind %d is not one of TEXTURE_2D, _2D_ARRAY, _3D or _CUBE", d.kind);
        return h;
    }
    if (d.width < 1 || d.height < 1 || d.width > 16384 || d.height > 16384) {
        gfx_misuse("a texture of %d x %d", d.width, d.height);
        return h;
    }
    if (d.kind == GS_GFX_TEXTURE_CUBE && d.width != d.height) {
        gfx_misuse("a cube texture must be square, not %d x %d", d.width, d.height);
        return h;
    }
    if (d.kind == GS_GFX_TEXTURE_2D || d.kind == GS_GFX_TEXTURE_CUBE) d.depth = 1;
    if (d.depth < 1 || d.depth > 2048) {
        gfx_misuse("a texture %s of %d", d.kind == GS_GFX_TEXTURE_3D ? "depth" : "layer count",
                   d.depth);
        return h;
    }
    if (d.samples != 2 && d.samples != 4 && d.samples != 8) d.samples = 1;
    int32_t largest = d.width > d.height ? d.width : d.height;
    if (d.kind == GS_GFX_TEXTURE_3D && d.depth > largest) largest = d.depth;
    int32_t chain = 1;
    while (largest >> chain) chain++;
    if (d.mips <= 0 || d.mips > chain) d.mips = chain;
    uint32_t usage = (uint32_t)d.usage;
    if (!usage || (usage & ~127u)) {
        gfx_misuse("texture usage %d is not a combination of the usage flags", d.usage);
        return h;
    }
    bool depth = gfx_is_depth(format);
    if (depth != ((usage & SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET) != 0)) {
        gfx_misuse(depth ? "a depth format needs DEPTH_TARGET usage"
                         : "DEPTH_TARGET usage needs a depth format");
        return h;
    }
    if (d.samples > 1 && (d.kind != GS_GFX_TEXTURE_2D || d.mips != 1 ||
                          (usage & ~(uint32_t)(SDL_GPU_TEXTUREUSAGE_COLOR_TARGET |
                                               SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)))) {
        gfx_misuse("a multisampled texture is a 2D target with one mip, drawn into and resolved, "
                   "not sampled or stored to");
        return h;
    }
    /* Generating mips draws into each level, which needs a color target. */
    if (d.mips > 1 && (usage & SDL_GPU_TEXTUREUSAGE_SAMPLER) && !depth)
        usage |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    SDL_GPUTextureType type = (SDL_GPUTextureType)d.kind;
    if (!SDL_GPUTextureSupportsFormat(gfx.dev, format, type, usage)) {
        gfx_fail("this device does not support texture format %d with usage %u", d.format, usage);
        return h;
    }
    SDL_GPUTextureCreateInfo ci;
    SDL_zero(ci);
    ci.type = type;
    ci.format = format;
    ci.usage = usage;
    ci.width = (Uint32)d.width;
    ci.height = (Uint32)d.height;
    ci.layer_count_or_depth = (Uint32)(d.kind == GS_GFX_TEXTURE_CUBE ? 6 : d.depth);
    ci.num_levels = (Uint32)d.mips;
    ci.sample_count = gfx_samples(d.samples);
    SDL_GPUTexture *tex = SDL_CreateGPUTexture(gfx.dev, &ci);
    if (!tex) {
        gfx_sdl_fail("creating a texture");
        return h;
    }
    gfx_texture_slot *s = (gfx_texture_slot *)gfx_table_add(&gfx.textures, &h.id);
    if (!s) {
        SDL_ReleaseGPUTexture(gfx.dev, tex);
        return h;
    }
    s->tex = tex;
    s->format = format;
    s->usage = usage;
    s->desc = d;
    s->desc.usage = (int32_t)usage;
    return h;
}

/* The region of `s` that `r` names, with w == 0 as the whole mip; a misuse
   if it is not all inside the texture. */
static bool gfx_region(gfx_texture_slot *s, const gs_gfx_region *r, SDL_GPUTextureRegion *out,
                       const char *what) {
    const gs_gfx_texture_desc *d = &s->desc;
    if (r->mip < 0 || r->mip >= d->mips)
        return gfx_misuse("%s: mip %d of a texture with %d", what, r->mip, d->mips);
    int32_t layers = d->kind == GS_GFX_TEXTURE_CUBE ? 6
                   : d->kind == GS_GFX_TEXTURE_2D_ARRAY ? d->depth : 1;
    if (r->layer < 0 || r->layer >= layers)
        return gfx_misuse("%s: layer %d of a texture with %d", what, r->layer, layers);
    int32_t mw = gfx_mip_size(d->width, r->mip), mh = gfx_mip_size(d->height, r->mip);
    int32_t md = d->kind == GS_GFX_TEXTURE_3D ? gfx_mip_size(d->depth, r->mip) : 1;
    SDL_zero(*out);
    out->texture = s->tex;
    out->mip_level = (Uint32)r->mip;
    out->layer = (Uint32)r->layer;
    if (r->w == 0) {
        out->w = (Uint32)mw;
        out->h = (Uint32)mh;
        out->d = (Uint32)md;
        return true;
    }
    int32_t h = r->h ? r->h : 1, dd = r->d ? r->d : 1;
    if (r->x < 0 || r->y < 0 || r->z < 0 || r->w < 0 || h < 0 || dd < 0 || r->x + r->w > mw ||
        r->y + h > mh || r->z + dd > md)
        return gfx_misuse("%s: region %d,%d,%d + %d x %d x %d is outside the %d x %d x %d mip",
                          what, r->x, r->y, r->z, r->w, h, dd, mw, mh, md);
    out->x = (Uint32)r->x;
    out->y = (Uint32)r->y;
    out->z = (Uint32)r->z;
    out->w = (Uint32)r->w;
    out->h = (Uint32)h;
    out->d = (Uint32)dd;
    return true;
}

static bool gfx_texture_bytes(gfx_texture_slot *s, const SDL_GPUTextureRegion *reg, int64_t len,
                              const char *what) {
    int64_t want = (int64_t)SDL_GPUTextureFormatTexelBlockSize(s->format) * reg->w * reg->h *
                   reg->d;
    if (len != want)
        return gfx_misuse("%s: %lld bytes for a %u x %u x %u region of %lld", what,
                          (long long)len, reg->w, reg->h, reg->d, (long long)want);
    return true;
}

static bool gfx_upload_texture(const SDL_GPUTextureRegion *reg, const void *data, uint32_t len,
                               bool fresh, const char *what) {
    SDL_GPUCommandBuffer *cb = gfx_copy_cmd(fresh, what);
    if (!cb) return false;
    SDL_GPUTransferBuffer *tb = gfx_transfer(len, true);
    if (!tb) {
        if (fresh) SDL_CancelGPUCommandBuffer(cb);
        return false;
    }
    void *p = SDL_MapGPUTransferBuffer(gfx.dev, tb, false);
    memcpy(p, data, len);
    SDL_UnmapGPUTransferBuffer(gfx.dev, tb);
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cb);
    SDL_GPUTextureTransferInfo src;
    SDL_zero(src);
    src.transfer_buffer = tb;
    src.pixels_per_row = reg->w;
    src.rows_per_layer = reg->h;
    SDL_UploadToGPUTexture(cp, &src, reg, false);
    SDL_EndGPUCopyPass(cp);
    SDL_ReleaseGPUTransferBuffer(gfx.dev, tb);
    return gfx_copy_done(cb, fresh);
}

uint8_t gs_gfx_update_texture(gs_gfx_texture t, const gs_gfx_region *region, gs_gfx_bytes data) {
    if (!gfx_need_device("update_texture")) return 0;
    gfx_texture_slot *s = gfx_texture_slot_of(t);
    if (!s) return 0;
    if (s->desc.samples > 1) return gfx_misuse("update_texture: a multisampled texture");
    SDL_GPUTextureRegion reg;
    if (!gfx_region(s, region, &reg, "update_texture") ||
        !gfx_texture_bytes(s, &reg, data.len, "update_texture"))
        return 0;
    return gfx_upload_texture(&reg, data.data, (uint32_t)data.len, false, "update_texture");
}

/* The pixels of a texture region, into `out`, waiting for the GPU. */
static bool gfx_download_texture(const SDL_GPUTextureRegion *reg, void *out, uint32_t len,
                                 const char *what) {
    SDL_GPUCommandBuffer *cb = gfx_copy_cmd(false, what);
    if (!cb) return false;
    SDL_GPUTransferBuffer *tb = gfx_transfer(len, false);
    if (!tb) return false;
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cb);
    SDL_GPUTextureTransferInfo dst;
    SDL_zero(dst);
    dst.transfer_buffer = tb;
    dst.pixels_per_row = reg->w;
    dst.rows_per_layer = reg->h;
    SDL_DownloadFromGPUTexture(cp, reg, &dst);
    SDL_EndGPUCopyPass(cp);
    bool ok = gfx_finish();
    if (ok) {
        void *p = SDL_MapGPUTransferBuffer(gfx.dev, tb, false);
        memcpy(out, p, len);
        SDL_UnmapGPUTransferBuffer(gfx.dev, tb);
    }
    SDL_ReleaseGPUTransferBuffer(gfx.dev, tb);
    return ok;
}

static bool gfx_readable(gfx_texture_slot *s, const char *what) {
    if (s->desc.samples > 1)
        return gfx_misuse("%s: a multisampled texture; read the texture it resolves to", what);
    if (gfx_is_depth(s->format)) return gfx_misuse("%s: a depth texture cannot be read back", what);
    return true;
}

uint8_t gs_gfx_read_texture(gs_gfx_texture t, const gs_gfx_region *region, gs_gfx_bytes out) {
    if (!gfx_need_device("read_texture")) return 0;
    gfx_texture_slot *s = gfx_texture_slot_of(t);
    if (!s || !gfx_readable(s, "read_texture")) return 0;
    SDL_GPUTextureRegion reg;
    if (!gfx_region(s, region, &reg, "read_texture") ||
        !gfx_texture_bytes(s, &reg, out.len, "read_texture"))
        return 0;
    return gfx_download_texture(&reg, out.data, (uint32_t)out.len, "read_texture");
}

uint8_t gs_gfx_save_png(gs_gfx_texture t, gs_gfx_bytes path) {
    if (!gfx_need_device("save_png")) return 0;
    gfx_texture_slot *s = gfx_texture_slot_of(t);
    if (!s || !gfx_readable(s, "save_png")) return 0;
    SDL_PixelFormat pf;
    if (s->format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM ||
        s->format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB)
        pf = SDL_PIXELFORMAT_RGBA32;
    else if (s->format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM)
        pf = SDL_PIXELFORMAT_BGRA32;
    else
        return gfx_misuse("save_png: only an RGBA8, RGBA8_SRGB or BGRA8 texture");
    gs_gfx_region r = { 0 };
    SDL_GPUTextureRegion reg;
    if (!gfx_region(s, &r, &reg, "save_png")) return 0;
    uint32_t len = reg.w * reg.h * 4;
    uint8_t *pixels = (uint8_t *)malloc(len);
    bool ok = gfx_download_texture(&reg, pixels, len, "save_png");
    /* The screen is saved as it is shown: opaque, whatever alpha drawing
       left in it. */
    if (ok && t.id == gfx.screen.id)
        for (uint32_t i = 3; i < len; i += 4) pixels[i] = 255;
    if (ok) {
        char buf[1024];
        size_t n = path.len < 0 ? 0 : (size_t)path.len;
        if (n >= sizeof buf) n = sizeof buf - 1;
        memcpy(buf, path.data, n);
        buf[n] = 0;
        SDL_Surface *surf = SDL_CreateSurfaceFrom((int)reg.w, (int)reg.h, pf, pixels,
                                                  (int)reg.w * 4);
        ok = surf && SDL_SavePNG(surf, buf);
        if (!ok) gfx_sdl_fail("save_png");
        SDL_DestroySurface(surf);
    }
    free(pixels);
    return ok;
}

gs_gfx_texture gs_gfx_load_texture(gs_gfx_bytes path, int64_t flags) {
    gs_gfx_texture h = { 0 };
    if (!gfx_need_device("load_texture")) return h;
    char buf[1024];
    size_t n = path.len < 0 ? 0 : (size_t)path.len;
    if (n >= sizeof buf) n = sizeof buf - 1;
    memcpy(buf, path.data, n);
    buf[n] = 0;
    bool bmp = n >= 4 && SDL_strcasecmp(buf + n - 4, ".bmp") == 0;
    SDL_Surface *loaded = bmp ? SDL_LoadBMP(buf) : SDL_LoadPNG(buf);
    if (!loaded) {
        gfx_fail("cannot load %s: %s", buf, SDL_GetError());
        return h;
    }
    SDL_Surface *rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (!rgba) {
        gfx_sdl_fail("converting an image");
        return h;
    }
    gs_gfx_texture_desc d;
    SDL_zero(d);
    d.kind = GS_GFX_TEXTURE_2D;
    d.format = (flags & GS_GFX_LOAD_SRGB) ? GS_GFX_RGBA8_SRGB : GS_GFX_RGBA8;
    d.usage = GS_GFX_SAMPLED;
    d.width = rgba->w;
    d.height = rgba->h;
    d.depth = 1;
    d.mips = (flags & GS_GFX_LOAD_MIPS) ? 0 : 1;
    d.samples = 1;
    h = gs_gfx_create_texture(&d);
    gfx_texture_slot *s = (gfx_texture_slot *)gfx_table_find(&gfx.textures, h.id);
    if (s) {
        /* Rows as the surface has them, which may be padded. */
        size_t row = (size_t)rgba->w * 4;
        uint8_t *tight = (uint8_t *)malloc(row * (size_t)rgba->h);
        for (int y = 0; y < rgba->h; y++)
            memcpy(tight + row * (size_t)y, (uint8_t *)rgba->pixels + (size_t)rgba->pitch * (size_t)y,
                   row);
        gs_gfx_region r = { 0 };
        SDL_GPUTextureRegion reg;
        gfx_region(s, &r, &reg, "load_texture");
        bool ok = gfx_upload_texture(&reg, tight, (uint32_t)(row * (size_t)rgba->h), true,
                                     "load_texture");
        free(tight);
        if (ok && s->desc.mips > 1) {
            SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer(gfx.dev);
            if (cb) {
                SDL_GenerateMipmapsForGPUTexture(cb, s->tex);
                SDL_SubmitGPUCommandBuffer(cb);
            }
        }
        if (!ok) {
            gs_gfx_release_texture(h);
            h.id = 0;
        }
    }
    SDL_DestroySurface(rgba);
    return h;
}

void gs_gfx_generate_mips(gs_gfx_texture t) {
    if (!gfx_need_device("generate_mips")) return;
    gfx_texture_slot *s = gfx_texture_slot_of(t);
    if (!s) return;
    if (s->desc.mips < 2 || !(s->usage & SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        gfx_misuse("generate_mips: a sampled texture with more than one mip");
        return;
    }
    SDL_GPUCommandBuffer *cb = gfx_copy_cmd(false, "generate_mips");
    if (cb) SDL_GenerateMipmapsForGPUTexture(cb, s->tex);
}

uint8_t gs_gfx_texture_info(gs_gfx_texture t, gs_gfx_texture_desc *out) {
    gfx_texture_slot *s = (gfx_texture_slot *)gfx_table_find(&gfx.textures, t.id);
    if (!s) return 0;
    *out = s->desc;
    return 1;
}

void gs_gfx_release_texture(gs_gfx_texture t) {
    if (!gfx_need_device("release_texture")) return;
    if (t.id == gfx.screen.id || t.id == gfx.screen_depth.id) {
        gfx_misuse("release_texture: the screen belongs to gfx");
        return;
    }
    gfx_texture_slot *s = gfx_texture_slot_of(t);
    if (!s) return;
    SDL_ReleaseGPUTexture(gfx.dev, s->tex);
    gfx_table_remove(&gfx.textures, t.id);
}

/* --- samplers -------------------------------------------------------------- */

gs_gfx_sampler gs_gfx_create_sampler(const gs_gfx_sampler_desc *desc) {
    gs_gfx_sampler h = { 0 };
    if (!gfx_need_device("sampler")) return h;
    const gs_gfx_sampler_desc *d = desc;
    if ((uint32_t)d->min_filter > 1 || (uint32_t)d->mag_filter > 1 || (uint32_t)d->mip_filter > 1 ||
        (uint32_t)d->wrap_u > 2 || (uint32_t)d->wrap_v > 2 || (uint32_t)d->wrap_w > 2 ||
        (uint32_t)d->compare > 8) {
        gfx_misuse("a sampler with filters, wraps or a compare that are not gfx's constants");
        return h;
    }
    SDL_GPUSamplerCreateInfo ci;
    SDL_zero(ci);
    ci.min_filter = (SDL_GPUFilter)d->min_filter;
    ci.mag_filter = (SDL_GPUFilter)d->mag_filter;
    ci.mipmap_mode = (SDL_GPUSamplerMipmapMode)d->mip_filter;
    ci.address_mode_u = (SDL_GPUSamplerAddressMode)d->wrap_u;
    ci.address_mode_v = (SDL_GPUSamplerAddressMode)d->wrap_v;
    ci.address_mode_w = (SDL_GPUSamplerAddressMode)d->wrap_w;
    ci.max_anisotropy = d->max_anisotropy;
    ci.enable_anisotropy = d->max_anisotropy > 1.0f;
    ci.compare_op = (SDL_GPUCompareOp)d->compare;
    ci.enable_compare = d->compare != 0;
    ci.min_lod = 0.0f;
    ci.max_lod = 1000.0f;
    SDL_GPUSampler *smp = SDL_CreateGPUSampler(gfx.dev, &ci);
    if (!smp) {
        gfx_sdl_fail("creating a sampler");
        return h;
    }
    gfx_sampler_slot *s = (gfx_sampler_slot *)gfx_table_add(&gfx.samplers, &h.id);
    if (!s) {
        SDL_ReleaseGPUSampler(gfx.dev, smp);
        return h;
    }
    s->smp = smp;
    return h;
}

void gs_gfx_release_sampler(gs_gfx_sampler smp) {
    if (!gfx_need_device("release_sampler")) return;
    gfx_sampler_slot *s = (gfx_sampler_slot *)gfx_table_get(&gfx.samplers, smp.id);
    if (!s) return;
    SDL_ReleaseGPUSampler(gfx.dev, s->smp);
    gfx_table_remove(&gfx.samplers, smp.id);
}

/* --- teardown -------------------------------------------------------------- */

static void gfx_free_buffer(void *item) {
    SDL_ReleaseGPUBuffer(gfx.dev, ((gfx_buffer_slot *)item)->buf);
}

static void gfx_free_texture(void *item) {
    SDL_ReleaseGPUTexture(gfx.dev, ((gfx_texture_slot *)item)->tex);
}

static void gfx_free_sampler(void *item) {
    SDL_ReleaseGPUSampler(gfx.dev, ((gfx_sampler_slot *)item)->smp);
}

void gfx_release_resources(void) {
    gfx_table_each(&gfx.buffers, gfx_free_buffer);
    gfx_table_each(&gfx.textures, gfx_free_texture);
    gfx_table_each(&gfx.samplers, gfx_free_sampler);
    gfx_table_clear(&gfx.buffers);
    gfx_table_clear(&gfx.textures);
    gfx_table_clear(&gfx.samplers);
}
