// The files a document imports, directly and indirectly, so that hover,
// definition and completion can see their declarations. Imports resolve like
// the compiler's: `import .a.b;` is a/b.goose beside the importing file, and
// `import a.b;` is relative to the program's root file, then the standard
// library. The server does not know the root file, so it uses the directories
// from the document up to the one holding gls.json (or three levels up
// if there is none).
import std;
import os;
import .scan;
import .protocol;
import .config;
import .diagnostics;

// Offsets into the name and text arenas the caller keeps. File 0 is the document.
struct WorkspaceFile {
    name_start: i64, name_end: i64, text_start: i64, text_end: i64
}

const MAX_WORKSPACE_FILES = 256;
const MAX_WORKSPACE_BYTES = 33554432;

fn add_directory(list: u8[>..]&, directory: const u8[:]) {
    list.append(directory);
    list.push(0);
}

var stdlib_cache_key: const u8[:] = "";
var stdlib_cache: const u8[:] = "";

// Asks the compiler where its standard library is, so the search order lives
// in one place. The answer is kept while the compiler and settings stay the same.
fn compiler_stdlib(compiler: const u8[:], configured: const u8[:], from_env: const u8[:], stdlib: u8[>..]&) {
    var name: u8[>..] = [];
    if compiler.len > 0 { name.append(compiler); }
    else if !env("GOOSE_COMPILER", name) || name.len == 0 { name.append("goose"); }
    var key: u8[>..] = [];
    format(key, name, "\n", configured, "\n", from_env);
    if key == stdlib_cache_key && stdlib_cache.len > 0 {
        add_directory(stdlib, stdlib_cache);
        return;
    }
    var argv: (const u8[:])[>..] = [];
    argv.push(name);
    if configured.len > 0 {
        argv.push("--stdlib");
        argv.push(configured);
    }
    argv.push("--print-stdlib");
    var output: u8[>..] = [];
    if run(argv, 5000, output) != 0 { return; }
    while output.len > 0 && (output[output.len - 1] == '\n' || output[output.len - 1] == '\r') {
        output.resize(output.len - 1);
    }
    if output.len == 0 { return; }
    if stdlib_cache_key.len > 0 {
        release_document_text(stdlib_cache_key);
        release_document_text(stdlib_cache);
    }
    stdlib_cache_key = own_document_text(key);
    stdlib_cache = own_document_text(output);
    add_directory(stdlib, output);
}

// Directories `import a.b;` is tried in, and the standard library's.
fn import_directories(document: const u8[:], roots: u8[>..]&, stdlib: u8[>..]&) {
    var settings = CheckSettings {};
    var compiler: u8[>..] = [];
    var configured_stdlib: u8[>..] = [];
    var entry: u8[>..] = [];
    var problem: u8[>..] = [];
    check_config(document, settings, compiler, configured_stdlib, entry, problem);
    var bytes: u8[>..] = [];
    var filename: u8[>..] = [];
    var project: u8[>..] = [];
    var selected = 0;
    var ignored: u8[>..] = [];
    let found = find_project_config(document, bytes, filename, selected, project, ignored);
    var directory: const u8[:] = config_parent(document);
    if entry.len > 0 { add_directory(roots, config_parent(entry)); }
    var levels = 0;
    loop {
        add_directory(roots, directory);
        levels++;
        let parent = config_parent(directory);
        if parent == directory || (found == 1 && directory == project) || (found != 1 && levels >= 3) {
            break;
        }
        directory = parent;
    }
    if configured_stdlib.len > 0 { add_directory(stdlib, configured_stdlib); }
    var from_env: u8[>..] = [];
    if env("GOOSE_STDLIB", from_env) && from_env.len > 0 { add_directory(stdlib, from_env); }
    compiler_stdlib(compiler, configured_stdlib, from_env, stdlib);
}

fn path_known(files: const WorkspaceFile[:], names: const u8[:], path: const u8[:]) -> bool {
    for f in files {
        if names[f.name_start..f.name_end] == path { return true; }
    }
    return false;
}

fn add_workspace_file(files: WorkspaceFile[>..]&, names: u8[>..]&, texts: u8[>..]&, path: const u8[:], text: const u8[:]) {
    let name_start = names.len;
    names.append(path);
    let text_start = texts.len;
    texts.append(text);
    files.push(WorkspaceFile {
        name_start: name_start, name_end: names.len, text_start: text_start, text_end: texts.len
    });
}

// The first directory in a NUL-separated list that holds a/b.goose, as a normalized path.
fn resolve_in(list: const u8[:], parts: const u8[:], out: u8[>..]&) -> bool {
    var start = 0;
    while start < list.len {
        var end = start;
        while end < list.len && list[end] != 0 { end++; }
        var raw: u8[>..] = [];
        format(raw, list[start..end], "/", parts, ".goose");
        let before = out.len;
        normalize_config_path(raw, out);
        if file_exists(out[before..]) { return true; }
        out.resize(before);
        start = end + 1;
    }
    return false;
}

// Appends the file `import` names, as a normalized path, if there is one.
fn resolve_import(relative: bool, parts: const u8[:], importer: const u8[:], roots: const u8[:], stdlib: const u8[:], out: u8[>..]&) -> bool {
    if relative {
        var raw: u8[>..] = [];
        format(raw, config_parent(importer), "/", parts, ".goose");
        normalize_config_path(raw, out);
        return file_exists(out);
    }
    return resolve_in(roots, parts, out) || resolve_in(stdlib, parts, out);
}

fn index_imports(document: const u8[:], text: const u8[:], files: WorkspaceFile[>..]&, names: u8[>..]&, texts: u8[>..]&) {
    var roots: u8[>..] = [];
    var stdlib: u8[>..] = [];
    import_directories(document, roots, stdlib);
    add_workspace_file(files, names, texts, document, text);
    var next = 0;
    while next < files.len && files.len < MAX_WORKSPACE_FILES && texts.len < MAX_WORKSPACE_BYTES {
        var importer: u8[>..] = [];
        importer.append(names[files[next].name_start..files[next].name_end]);
        var source: u8[>..] = [];
        source.append(texts[files[next].text_start..files[next].text_end]);
        next++;
        var ts: SourceToken[>..] = [];
        scan_source(source, ts);
        var depth = 0;
        var i = 0;
        while i < ts.len {
            let word = token_text(source, ts[i]);
            if word == "{" { depth++; }
            if word == "}" { depth--; }
            i++;
            if depth != 0 || word != "import" || ts[i - 1].kind != NAME_TOKEN { continue; }
            var n = i;
            var relative = false;
            if n < ts.len && token_text(source, ts[n]) == "." {
                relative = true;
                n++;
            }
            var parts: u8[>..] = [];
            while n < ts.len && ts[n].kind == NAME_TOKEN {
                parts.append(token_text(source, ts[n]));
                n++;
                if n < ts.len && token_text(source, ts[n]) == "." {
                    parts.push('/');
                    n++;
                } else { break; }
            }
            if parts.len == 0 || n >= ts.len || token_text(source, ts[n]) != ";" { continue; }
            var path: u8[>..] = [];
            if !resolve_import(relative, parts, importer, roots, stdlib, path) { continue; }
            if path_known(files, names, path) { continue; }
            var buffer: u8[>..] = [];
            add_workspace_file(files, names, texts, path, file_text(path, buffer));
        }
    }
}
