// Files are loaded iteratively: import cycles do not recurse or reload files.
fn directory(ctx: Compiler&, path: const u8[:]) -> const u8[:] {
    var last = 0;
    for c, i in path { if c == '/' || c == '\\' { last = i + 1; } }
    return path[..last];
}
fn normalized(ctx: Compiler&, path: const u8[:]) -> u8[>..] {
    var result: u8[>..] = [];
    var components: i64[>..<] = [];
    var i = 0;
    if path.len >= 2 && path[1] == ':' { result.append(path[..2]); i = 2; }
    let absolute = path.len > i && (path[i] == '/' || path[i] == '\\');
    if absolute { result.push('/'); }
    while i < path.len {
        while i < path.len && (path[i] == '/' || path[i] == '\\') { i++; }
        let start = i;
        while i < path.len && path[i] != '/' && path[i] != '\\' { i++; }
        let part = path[start..i];
        if part.len == 0 || part == "." { continue; }
        if part == ".." && components.len > 0 {
            var previous = components[components.len - 1];
            if previous < result.len && result[previous] == '/' { previous++; }
            let parent = result.len - previous == 2 && result[previous] == '.' && result[previous + 1] == '.';
            if !parent { let p = components.pop(); result.resize(p); continue; }
        }
        if part == ".." && components.len == 0 && absolute { continue; }
        let base = result.len;
        if result.len > 0 && result[result.len - 1] != '/' { result.push('/'); }
        components.push(base); result.append(part);
    }
    return result;
}
fn load_file(ctx: Compiler&, path: const u8[:]) -> i64 {
    let clean = normalized(ctx, path);
    for s, i in ctx.sources { if s.path == clean { return i; } }
    let name = own_text(ctx, clean);
    let begin = ctx.text_storage.len;
    if !read_file(name, ctx.text_storage) { write_stderr(str("cannot read ", name, "\n")); exit(1); }
    let id = ctx.sources.len;
    ctx.sources.push(Source { path: name, text: ctx.text_storage[begin..] });
    lex(ctx, id);
    return id;
}
fn import_file(ctx: Compiler&, n: i64, stdlib_path: const u8[:]) -> i64 {
    var relative: u8[>..] = [];
    var e = ctx.nodes[n].first;
    while e >= 0 {
        if relative.len > 0 { relative.push('/'); }
        relative.append(ctx.tokens[ctx.nodes[ctx.edges[e].value].token].text);
        e = ctx.edges[e].next;
    }
    relative.append(".goose");
    var base = 0;
    if ctx.nodes[n].flags & 1 != 0 { base = ctx.tokens[ctx.nodes[n].token].file; }
    let local = str(directory(ctx, ctx.sources[base].path), relative);
    if file_exists(local) { return load_file(ctx, local); }
    if ctx.nodes[n].flags & 1 == 0 {
        let library = str(stdlib_path, "/", relative);
        if file_exists(library) { return load_file(ctx, library); }
    }
    error_at(ctx, ctx.nodes[n].token, str("cannot find import ", relative));
    return -1;
}
fn load_program(ctx: Compiler&, path: const u8[:], stdlib_path: const u8[:]) {
    load_file(ctx, path);
    var file = 0;
    while file < ctx.sources.len {
        let start = ctx.declarations.len;
        parse(ctx, file);
        let limit = ctx.declarations.len;
        for i in start..limit {
            let n = ctx.declarations[i];
            if ctx.nodes[n].tag == N_IMPORT { ctx.nodes[n].a = import_file(ctx, n, stdlib_path); }
        }
        file++;
    }
}
