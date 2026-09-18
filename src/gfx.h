// The compiler's side of the optional graphics module (stdlib/gfx.goose):
// compiling the shaders `embed_shader` embeds, and -- when SDL3 was built in
// -- handing the gfx layer's C functions to a JIT run.
//
// Shader compilation (src/shaderc.c) is always built: a program using gfx
// typechecks and emits the same C with or without SDL. Only running one in
// this process needs the layer itself, which is what `have_gfx` says.

#pragma once

#include "shaderc.h"
#include "gfx/gfx_blob.h"

namespace goose {

#ifdef GOOSE_HAVE_GFX
inline constexpr bool have_gfx = true;
#else
inline constexpr bool have_gfx = false;
#endif

// The stage a shader file is for, from its extension; -1 for none.
inline int ShaderStageOf(const string &path) {
    auto dot = path.find_last_of('.');
    auto ext = dot == string::npos ? string() : path.substr(dot);
    if (ext == ".vert") return GS_GFX_STAGE_VERTEX;
    if (ext == ".frag") return GS_GFX_STAGE_FRAGMENT;
    if (ext == ".comp") return GS_GFX_STAGE_COMPUTE;
    return -1;
}

// The blob for the shader file at `path` (src/gfx/gfx_blob.h). A shader that
// does not compile is a CompileError carrying the shader's own file:line
// message.
inline string CompileShaderFile(const string &path) {
    auto stage = ShaderStageOf(path);
    if (stage < 0)
        throw CompileError { cat("cannot tell the shader stage of ", path,
                                 ": the extension must be .vert, .frag or .comp") };
    string src;
    if (!LoadFile(path, src)) throw CompileError { cat("cannot open shader file: ", path) };
    auto r = gs_shaderc_compile(src.c_str(), path.c_str(), stage);
    if (!r.blob) {
        auto msg = string(r.error ? r.error : "shader compilation failed");
        gs_shaderc_free(&r);
        throw CompileError { msg };
    }
    string blob((const char *)r.blob, r.size);
    gs_shaderc_free(&r);
    return blob;
}

// --compile-shader: what a blob holds, for looking at a shader without a
// program around it; `source` optionally prints the MSL or HLSL as well.
inline void DumpShader(const string &path, const string &source) {
    auto blob = CompileShaderFile(path);
    gs_gfx_blob_header h;
    memcpy(&h, blob.data(), sizeof h);
    static const char *stages[] = { "vertex", "fragment", "compute" };
    string s = cat(stages[h.stage], " shader, ", blob.size(), " bytes: SPIR-V ", h.spirv_size,
                   ", MSL ", h.msl_size, ", HLSL ", h.hlsl_size, "\n");
    Append(s, "samplers ", h.num_samplers, ", storage textures ", h.num_storage_textures_ro,
           " ro / ", h.num_storage_textures_rw, " rw, storage buffers ", h.num_storage_buffers_ro,
           " ro / ", h.num_storage_buffers_rw, " rw, uniform blocks ", h.num_uniform_buffers);
    auto p = blob.data() + sizeof h;
    for (int i = 0; i < h.num_uniform_buffers; i++, p += 4) {
        uint32_t size;
        memcpy(&size, p, 4);
        Append(s, i ? ", " : " (", size, " bytes", i + 1 == h.num_uniform_buffers ? ")" : "");
    }
    s += "\n";
    if (h.stage == GS_GFX_STAGE_COMPUTE)
        Append(s, "local size ", h.local_size[0], " x ", h.local_size[1], " x ",
               h.local_size[2], "\n");
    for (int i = 0; i < h.num_inputs; i++, p += sizeof(gs_gfx_blob_input)) {
        gs_gfx_blob_input in;
        memcpy(&in, p, sizeof in);
        Append(s, "input location ", (int)in.location, " type ", (int)in.type, "\n");
    }
    if (source == "msl") Append(s, string_view(blob).substr(h.msl_offset, h.msl_size));
    else if (source == "hlsl") Append(s, string_view(blob).substr(h.hlsl_offset, h.hlsl_size));
    else if (!source.empty())
        throw CompileError { cat("--shader-source takes msl or hlsl, not ", source) };
    fputs(s.c_str(), stdout);
}

}  // namespace goose
