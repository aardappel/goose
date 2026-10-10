// The compiler's side of embed_slang (stdlib/ngfx.goose): compiling one Slang
// module, every entry point of it, for both of NoGraphicsAPI's backends into
// the blob src/ngfx/ngfx_blob.h describes.
//
// Unlike embed_shader's compiler, this one is not built in: it runs the tools
// NoGraphicsAPI's own build runs (examples/NoGraphicsAPI_slang.cmake), with
// the same options, as subprocesses. slangc makes SPIR-V and, on a Mac, the
// Metal source that Xcode's `metal` and `metallib` turn into a metallib. Each
// tool is found through an environment variable, else where CMake found it,
// else on PATH:
//
//   GOOSE_SLANGC              slangc (2026.18.2 or later)
//   GOOSE_SPIRV_VAL           spirv-val, optional: validates the SPIR-V
//   GOOSE_NOGRAPHICSAPI       NoGraphicsAPI's source tree, for the includes
//                             <NoGraphicsAPI/shader.slang> and
//                             <NoGraphicsAPIUtility/shader_types.h>
//
// The blob always holds SPIR-V. It holds a metallib when the compiler runs on
// a Mac, where that is the backend a program runs on, so there a missing
// Metal toolchain is an error; elsewhere the Metal section is left empty.

#pragma once

#include <filesystem>
#include <atomic>
#include "ngfx/ngfx_blob.h"

#ifdef _WIN32
    #include <process.h>
    #define GOOSE_POPEN _popen
    #define GOOSE_PCLOSE _pclose
#else
    #include <unistd.h>
    #define GOOSE_POPEN popen
    #define GOOSE_PCLOSE pclose
#endif

namespace goose {

// A tool's path: the environment's, else the one CMake found, else its name.
inline string SlangTool(const char *env, const char *configured, const char *name) {
    if (auto e = getenv(env); e && *e) return e;
    if (configured && *configured) return configured;
    return name;
}

inline string SlangcPath() {
    #ifdef GOOSE_SLANGC
        return SlangTool("GOOSE_SLANGC", GOOSE_SLANGC, "slangc");
    #else
        return SlangTool("GOOSE_SLANGC", nullptr, "slangc");
    #endif
}

inline string SpirvValPath() {
    #ifdef GOOSE_SPIRV_VAL
        return SlangTool("GOOSE_SPIRV_VAL", GOOSE_SPIRV_VAL, "");
    #else
        return SlangTool("GOOSE_SPIRV_VAL", nullptr, "");
    #endif
}

inline string NoGraphicsApiDir() {
    #ifdef GOOSE_NOGRAPHICSAPI
        return SlangTool("GOOSE_NOGRAPHICSAPI", GOOSE_NOGRAPHICSAPI, "");
    #else
        return SlangTool("GOOSE_NOGRAPHICSAPI", nullptr, "");
    #endif
}

// Runs a command, its output and errors together into `out`: whether it
// exited with 0.
inline bool RunTool(const vector<string> &argv, string &out) {
    string cmd;
    for (auto &a : argv) {
        if (!cmd.empty()) cmd += ' ';
        #ifdef _WIN32
            cmd += '"' + a + '"';
        #else
            cmd += '\'';
            for (char ch : a) {
                if (ch == '\'') cmd += "'\\''";
                else cmd += ch;
            }
            cmd += '\'';
        #endif
    }
    cmd += " 2>&1";
    #ifdef _WIN32
        cmd = '"' + cmd + '"';   // cmd /c strips one pair of quotes around the whole.
    #endif
    out.clear();
    FILE *p = GOOSE_POPEN(cmd.c_str(), "r");
    if (!p) return false;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
    return GOOSE_PCLOSE(p) == 0;
}

// Just enough JSON for slangc's reflection: its entry points' names, stages
// and thread group sizes.
struct SlangJson {
    enum Kind { NUL, NUM, STR, ARR, OBJ, OTHER } kind = NUL;
    double num = 0;
    string str;
    vector<SlangJson> items;
    vector<string> keys;   // OBJ: items[i] is keys[i]'s value.

    const SlangJson *Get(string_view key) const {
        for (size_t i = 0; i < keys.size(); i++) if (keys[i] == key) return &items[i];
        return nullptr;
    }

    static void Space(string_view &s) {
        while (!s.empty() && isspace((uint8_t)s[0])) s.remove_prefix(1);
    }

    static bool Parse(string_view &s, SlangJson &v) {
        Space(s);
        if (s.empty()) return false;
        if (s[0] == '{' || s[0] == '[') {
            bool obj = s[0] == '{';
            v.kind = obj ? OBJ : ARR;
            s.remove_prefix(1);
            Space(s);
            if (!s.empty() && s[0] == (obj ? '}' : ']')) { s.remove_prefix(1); return true; }
            for (;;) {
                if (obj) {
                    SlangJson key;
                    if (!Parse(s, key) || key.kind != STR) return false;
                    Space(s);
                    if (s.empty() || s[0] != ':') return false;
                    s.remove_prefix(1);
                    v.keys.push_back(key.str);
                }
                v.items.emplace_back();
                if (!Parse(s, v.items.back())) return false;
                Space(s);
                if (s.empty()) return false;
                if (s[0] == ',') { s.remove_prefix(1); continue; }
                if (s[0] != (obj ? '}' : ']')) return false;
                s.remove_prefix(1);
                return true;
            }
        }
        if (s[0] == '"') {
            v.kind = STR;
            s.remove_prefix(1);
            while (!s.empty() && s[0] != '"') {
                if (s[0] == '\\' && s.size() > 1) {
                    char e = s[1];
                    v.str += e == 'n' ? '\n' : e == 't' ? '\t' : e;
                    s.remove_prefix(e == 'u' && s.size() >= 6 ? 6 : 2);
                } else {
                    v.str += s[0];
                    s.remove_prefix(1);
                }
            }
            if (s.empty()) return false;
            s.remove_prefix(1);
            return true;
        }
        if (s[0] == '-' || isdigit((uint8_t)s[0])) {
            v.kind = NUM;
            size_t n = 0;
            while (n < s.size() && (isdigit((uint8_t)s[n]) || strchr("+-.eE", s[n]))) n++;
            v.num = strtod(string(s.substr(0, n)).c_str(), nullptr);
            s.remove_prefix(n);
            return true;
        }
        v.kind = OTHER;   // true, false, null
        while (!s.empty() && isalpha((uint8_t)s[0])) s.remove_prefix(1);
        return true;
    }
};

// slangc's diagnostic, "error[E30015]: undefined identifier\n --> f.slang:3:36\n
// ... ^^^ undefined identifier 'foo'.", as "f.slang:3: error: undefined
// identifier 'foo'." -- the form embed_shader's errors take, which
// ShaderMessageAt reads. Output it cannot read comes back as it is.
inline string SlangMessage(const string &out) {
    vector<string_view> lines;
    for (size_t at = 0; at < out.size();) {
        auto nl = out.find('\n', at);
        if (nl == string::npos) nl = out.size();
        lines.push_back(string_view(out).substr(at, nl - at));
        at = nl + 1;
    }
    for (size_t i = 0; i < lines.size(); i++) {
        auto l = lines[i];
        if (l.substr(0, 5) != "error") continue;
        auto colon = l.find(": ");
        string headline(colon == string_view::npos ? l : l.substr(colon + 2));
        string where, detail;
        for (size_t j = i + 1; j < lines.size() && !lines[j].starts_with("error"); j++) {
            auto m = lines[j];
            if (auto arrow = m.find("--> "); arrow != string_view::npos && where.empty()) {
                // path:line:column, where the path may hold colons itself.
                auto loc = m.substr(arrow + 4);
                auto c2 = loc.rfind(':');
                auto c1 = c2 == string_view::npos ? c2 : loc.rfind(':', c2 - 1);
                where = c1 == string_view::npos ? string(loc) : string(loc.substr(0, c2));
            } else if (auto caret = m.find('^'); caret != string_view::npos && detail.empty()) {
                auto d = m.substr(caret);
                while (!d.empty() && (d[0] == '^' || d[0] == ' ')) d.remove_prefix(1);
                detail = string(d);
            }
        }
        auto msg = detail.empty() ? headline : detail;
        return where.empty() ? msg : cat(where, ": error: ", msg);
    }
    auto s = out;
    while (!s.empty() && isspace((uint8_t)s.back())) s.pop_back();
    return s;
}

// A directory of its own for one compilation's files, removed when done.
struct SlangTemp {
    std::filesystem::path dir;
    SlangTemp() {
        static std::atomic<int> counter { 0 };
        #ifdef _WIN32
            auto pid = (long)_getpid();
        #else
            auto pid = (long)getpid();
        #endif
        dir = std::filesystem::temp_directory_path() /
              cat("goose-slang-", pid, "-", counter++);
        std::filesystem::create_directories(dir);
    }
    ~SlangTemp() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    string operator()(const char *name) const { return (dir / name).string(); }
};

inline uint32_t SlangStage(const string &name) {
    if (name == "vertex") return GS_NGFX_BLOB_STAGE_VERTEX;
    if (name == "fragment" || name == "pixel") return GS_NGFX_BLOB_STAGE_FRAGMENT;
    if (name == "compute") return GS_NGFX_BLOB_STAGE_COMPUTE;
    return UINT32_MAX;
}

// The blob for the Slang module in the file at `path`, whose #include also
// looks in `includedir`. A module that does not compile is a CompileError
// carrying "path:line: error: message" when slangc says where.
inline string CompileSlangFile(const string &path, const string &includedir) {
    SlangTemp tmp;
    auto slangc = SlangcPath();
    vector<string> common = { slangc, path, "-fvk-use-c-layout", "-matrix-layout-row-major" };
    if (!includedir.empty()) { common.push_back("-I"); common.push_back(includedir); }
    auto ng = NoGraphicsApiDir();
    if (!ng.empty()) {
        for (auto inc : { "/include", "/utility/include" }) {
            common.push_back("-I");
            common.push_back(ng + inc);
        }
    }
    string out;

    const char *no_entries = "the module has no entry points: mark each with [shader(\"vertex\")], "
                             "[shader(\"fragment\")] or [shader(\"compute\")]";
    // SPIR-V, and the reflection that lists the entry points.
    auto spirv = [&]() {
        auto argv = common;
        for (auto a : { "-target", "spirv", "-profile", "spirv_1_5", "-emit-spirv-directly",
                        "-fvk-use-entrypoint-name", "-capability", "spvDescriptorHeapEXT" })
            argv.push_back(a);
        argv.push_back("-o");
        argv.push_back(tmp("module.spv"));
        argv.push_back("-reflection-json");
        argv.push_back(tmp("reflection.json"));
        if (!RunTool(argv, out)) {
            if (out.find("error") == string::npos)
                throw CompileError { cat("cannot run slangc (", slangc, "; set GOOSE_SLANGC): ",
                                         SlangMessage(out)) };
            if (out.find("contains no exported symbols") != string::npos)
                throw CompileError { no_entries };
            throw CompileError { SlangMessage(out) };
        }
    };
    spirv();
    string json;
    if (!LoadFile(tmp("reflection.json"), json))
        throw CompileError { "slangc wrote no reflection" };
    SlangJson root;
    string_view js = json;
    if (!SlangJson::Parse(js, root) || root.kind != SlangJson::OBJ)
        throw CompileError { "cannot read slangc's reflection" };
    vector<gs_ngfx_blob_entry> entries;
    if (auto eps = root.Get("entryPoints"); eps && eps->kind == SlangJson::ARR) {
        for (auto &ep : eps->items) {
            auto name = ep.Get("name");
            auto stage = ep.Get("stage");
            if (!name || !stage) continue;
            gs_ngfx_blob_entry e {};
            if (name->str.size() >= GS_NGFX_BLOB_NAME_SIZE)
                throw CompileError { cat("the entry point name ", name->str, " is longer than ",
                                         GS_NGFX_BLOB_NAME_SIZE - 1, " characters") };
            memcpy(e.name, name->str.data(), name->str.size());
            e.stage = SlangStage(stage->str);
            if (e.stage == UINT32_MAX)
                throw CompileError { cat("the entry point ", name->str, " is a ", stage->str,
                                         " shader; ngfx takes vertex, fragment and compute shaders") };
            e.threadgroup[0] = e.threadgroup[1] = e.threadgroup[2] = 1;
            if (auto tg = ep.Get("threadGroupSize"); tg && tg->kind == SlangJson::ARR)
                for (size_t i = 0; i < 3 && i < tg->items.size(); i++)
                    e.threadgroup[i] = (uint32_t)tg->items[i].num;
            entries.push_back(e);
        }
    }
    if (entries.empty()) throw CompileError { no_entries };
    string spv;
    if (!LoadFile(tmp("module.spv"), spv)) throw CompileError { "slangc wrote no SPIR-V" };
    if (auto val = SpirvValPath(); !val.empty()) {
        if (!RunTool({ val, "--target-env", "vulkan1.4", "--scalar-block-layout",
                       tmp("module.spv") }, out))
            throw CompileError { cat("spirv-val rejects the SPIR-V slangc made: ",
                                     SlangMessage(out)) };
    }

    // The metallib, on a Mac.
    string metallib;
    #ifdef __APPLE__
        auto argv = common;
        for (auto a : { "-target", "metal", "-DNOGRAPHICSAPI_METAL" }) argv.push_back(a);
        argv.push_back("-o");
        argv.push_back(tmp("module.metal"));
        if (!RunTool(argv, out)) throw CompileError { SlangMessage(out) };
        if (!RunTool({ "xcrun", "-sdk", "macosx", "metal", "-std=metal4.0", "-c",
                       tmp("module.metal"), "-o", tmp("module.air") }, out))
            throw CompileError { cat("Xcode's Metal compiler failed on slangc's output (install it "
                                     "with xcodebuild -downloadComponent MetalToolchain): ",
                                     SlangMessage(out)) };
        if (!RunTool({ "xcrun", "-sdk", "macosx", "metallib", tmp("module.air"), "-o",
                       tmp("module.metallib") }, out))
            throw CompileError { cat("metallib failed: ", SlangMessage(out)) };
        if (!LoadFile(tmp("module.metallib"), metallib))
            throw CompileError { "metallib wrote nothing" };
    #endif

    // header | entries | SPIR-V | metallib, each section 16-byte aligned.
    auto align = [](size_t n) { return (n + 15) & ~(size_t)15; };
    gs_ngfx_blob_header h {};
    h.magic = GS_NGFX_BLOB_MAGIC;
    h.version = GS_NGFX_BLOB_VERSION;
    h.entry_count = (uint32_t)entries.size();
    h.spirv_offset = (uint32_t)align(sizeof h + entries.size() * sizeof(gs_ngfx_blob_entry));
    h.spirv_size = (uint32_t)spv.size();
    h.metallib_offset = (uint32_t)align(h.spirv_offset + spv.size());
    h.metallib_size = (uint32_t)metallib.size();
    string blob(h.metallib_offset + metallib.size(), '\0');
    memcpy(blob.data(), &h, sizeof h);
    memcpy(blob.data() + sizeof h, entries.data(), entries.size() * sizeof(gs_ngfx_blob_entry));
    memcpy(blob.data() + h.spirv_offset, spv.data(), spv.size());
    memcpy(blob.data() + h.metallib_offset, metallib.data(), metallib.size());
    return blob;
}

// The blob for Slang `source` written in the file `path`: compiled from a
// file of its own, its errors put back at `path`, with #include looking in
// `path`'s directory.
inline string CompileSlangSource(const string &source, const string &path) {
    SlangTemp tmp;
    auto file = tmp("embedded.slang");
    if (FILE *f = fopen(file.c_str(), "wb")) {
        fwrite(source.data(), 1, source.size(), f);
        fclose(f);
    } else {
        throw CompileError { cat("cannot write ", file) };
    }
    auto dir = std::filesystem::absolute(path).parent_path().string();
    try {
        return CompileSlangFile(file, dir);
    } catch (CompileError &e) {
        if (e.msg.compare(0, file.size(), file) == 0) e.msg = path + e.msg.substr(file.size());
        throw;
    }
}

}  // namespace goose
