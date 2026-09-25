// Clean-room compiler model. No compiler sources in src/ are used.
// All long-lived storage is append-only. Indices keep nodes freely mutable
// and let one node occur in several lists without intrusive sibling links.
struct Source {
    path: const u8[:],
    text: const u8[:],
    first: i64 = 0,
    limit: i64 = 0,
    visited: i64 = 0,
}

struct Token {
    kind: i64,
    text: const u8[:],
    file: i64,
    offset: i64,
    line: i64,
    column: i64,
    integer: u64 = 0,
}

// Tags are named constants so debugging a node never requires a payload
// reference into a mutable fixed-mode ADT.
const N_NAME = 1;
const N_INT = 2;
const N_FLOAT = 3;
const N_STRING = 4;
const N_BOOL = 5;
const N_NULL = 6;
const N_UNARY = 7;
const N_BINARY = 8;
const N_CALL = 9;
const N_MEMBER = 10;
const N_INDEX = 11;
const N_SLICE = 12;
const N_CAST = 13;
const N_ARRAY = 14;
const N_FILL = 15;
const N_LIMITED = 16;
const N_CONSTRUCT = 17;
const N_INIT = 18;
const N_BLOCK = 19;
const N_IF = 20;
const N_WHILE = 21;
const N_FOR = 22;
const N_LOOP = 23;
const N_EARLY = 24;
const N_RETURN = 25;
const N_BREAK = 26;
const N_CONTINUE = 27;
const N_GUARD = 28;
const N_MATCH = 29;
const N_ARM = 30;
const N_BIND = 31;
const N_ASSIGN = 32;
const N_FUNCTION = 33;
const N_PARAM = 34;
const N_STRUCT = 35;
const N_ENUM = 36;
const N_VARIANT = 37;
const N_FIELD = 38;
const N_ALIAS = 39;
const N_IMPORT = 40;
const N_TYPE = 41;
const N_TYPE_ARRAY = 42;
const N_TYPE_REF = 43;
const N_TYPE_SLICE = 44;
const N_TYPE_OPTIONAL = 45;
const N_TYPE_MODE = 46;
const N_TYPE_MEMBER = 47;
const N_GENERIC = 48;
const N_FUNVAL = 49;
const N_EXPR_STMT = 50;
const N_RANGE = 51;
const N_PAD = 52;
const N_SELF = 53;

struct Node {
    tag: i64,
    token: i64,
    a: i64 = -1,
    b: i64 = -1,
    c: i64 = -1,
    first: i64 = -1,
    last: i64 = -1,
    flags: i64 = 0,
    ty: i64 = -1,
    symbol: i64 = -1,
    ns: i64 = -1,
    target: i64 = -1,
    root: i64 = -1,
    place: i64 = -1,
    readonly: bool = false,
    growshrink: bool = false,
    inexact: bool = false,
    known: bool = false,
    bits: u64 = 0,
    held: i64 = -1,
    fields: i64 = -1,
    standalone: bool = false,
}
struct Edge { value: i64, next: i64 = -1 }


fn node(ctx: Compiler&, tag: i64, token: i64) -> i64 {
    let id = ctx.nodes.len;
    ctx.nodes.push(Node { tag: tag, token: token, ns: ctx.current_namespace });
    return id;
}
fn child(ctx: Compiler&, parent: i64, value: i64) {
    let e = ctx.edges.len;
    ctx.edges.push(Edge { value: value });
    if ctx.nodes[parent].last >= 0 { ctx.edges[ctx.nodes[parent].last].next = e; }
    else { ctx.nodes[parent].first = e; }
    ctx.nodes[parent].last = e;
}
fn own_text(ctx: Compiler&, s: const u8[:]) -> const u8[:] {
    let begin = ctx.text_storage.len;
    ctx.text_storage.append(s);
    return ctx.text_storage[begin..];
}
fn diagnostic(ctx: Compiler&, file: i64, line: i64, column: i64, message: const u8[:]) {
    write_stderr(str(ctx.sources[file].path, ":", line, ":", column, ": error: ", message, "\n"));
    exit(1);
}
fn error_at(ctx: Compiler&, t: i64, message: const u8[:]) {
    diagnostic(ctx, ctx.tokens[t].file, ctx.tokens[t].line, ctx.tokens[t].column, message);
}
