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

// What running a gfx program in this process says when the layer is not
// built in. The test runners report it as a skip.
inline const char *no_gfx_error = "this compiler was built without SDL3; check out "
                                  "third_party/SDL and reconfigure";

// The response file of link inputs a program built from the generated C
// needs to use gfx (cmake/gfx.cmake): `style` is "msvc" for cl and clang-cl,
// "cc" for gcc and clang. An explicit override for a moved build tree, next
// to the compiler binary for an installed one, and otherwise where CMake
// wrote them.
inline string GfxLinkFile(const string &exedir, const string &style) {
    if (style != "msvc" && style != "cc")
        throw CompileError { cat("--gfx-link takes msvc or cc, not ", style) };
    if (!have_gfx) throw CompileError { no_gfx_error };
    auto name = cat("link-", style, ".rsp");
    vector<string> dirs;
    if (auto env = getenv("GOOSE_GFX_LINK")) dirs.push_back(env);
    dirs.push_back(cat(exedir, "gfx"));
    #ifdef GOOSE_GFX_LINK_PATH
        dirs.push_back(GOOSE_GFX_LINK_PATH);
    #endif
    for (auto &dir : dirs) {
        auto path = cat(dir, "/", name);
        if (auto f = fopen(path.c_str(), "rb")) {
            fclose(f);
            return path;
        }
    }
    throw CompileError { cat("cannot find ", name, " (set GOOSE_GFX_LINK to its directory)") };
}

// The stage embed_shader("frag", source) names, which is also the extension
// of a shader file for it; -1 for none.
inline int ShaderStageNamed(string_view name) {
    if (name == "vert") return GS_GFX_STAGE_VERTEX;
    if (name == "frag") return GS_GFX_STAGE_FRAGMENT;
    if (name == "comp") return GS_GFX_STAGE_COMPUTE;
    return -1;
}

// The stage a shader file is for, from its extension; -1 for none.
inline int ShaderStageOf(const string &path) {
    auto dot = path.find_last_of('.');
    return dot == string::npos ? -1 : ShaderStageNamed(string_view(path).substr(dot + 1));
}

// Where embed_shader(`lit`) in the file `from` finds its shader: relative to
// that file's directory, the way `import .x` resolves, unless absolute.
inline string EmbeddedShaderPath(const string &from, string_view lit) {
    auto absolute = (!lit.empty() && (lit[0] == '/' || lit[0] == '\\')) ||
                    (lit.size() > 1 && lit[1] == ':');
    if (absolute) return string(lit);
    auto pos = from.find_last_of("/\\");
    return cat(pos == string::npos ? string() : from.substr(0, pos + 1), lit);
}

// The blob (src/gfx/gfx_blob.h) for the GLSL `source` of a shader for
// `stage`, which `path` names in messages and `#include` resolves relative
// to. A shader that does not compile is a CompileError carrying the shader
// compiler's message: "path:line: error: ..." for one at a line of
// `source`, "path: ..." for one about the shader as a whole, and the
// included file's name for one inside an include.
inline string CompileShader(const string &source, const string &path, int stage) {
    auto r = gs_shaderc_compile(source.c_str(), path.c_str(), stage);
    if (!r.blob) {
        auto msg = string(r.error ? r.error : "shader compilation failed");
        gs_shaderc_free(&r);
        throw CompileError { msg };
    }
    string blob((const char *)r.blob, r.size);
    gs_shaderc_free(&r);
    return blob;
}

// The blob for the shader file at `path`.
inline string CompileShaderFile(const string &path) {
    auto stage = ShaderStageOf(path);
    if (stage < 0)
        throw CompileError { cat("cannot tell the shader stage of ", path,
                                 ": the extension must be .vert, .frag or .comp") };
    string src;
    if (!LoadFile(path, src)) throw CompileError { cat("cannot open shader file: ", path) };
    return CompileShader(src, path, stage);
}

// Takes apart a CompileShader message about `path`'s own source: the line
// it is at (0 for none) and the message without its location. False for a
// message about an included file.
inline bool ShaderMessageAt(const string &err, const string &path, int &line, string &msg) {
    if (err.compare(0, path.size(), path) || err.size() < path.size() + 2 ||
        err[path.size()] != ':')
        return false;
    auto rest = string_view(err).substr(path.size() + 1);
    line = 0;
    while (!rest.empty() && isdigit((uint8_t)rest[0])) {
        line = line * 10 + (rest[0] - '0');
        rest.remove_prefix(1);
    }
    auto sep = string_view(line ? ": error: " : " ");
    if (rest.substr(0, sep.size()) != sep) return false;
    msg = string(rest.substr(sep.size()));
    return true;
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
