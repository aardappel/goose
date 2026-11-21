// lsp: fixture for test/lsp_protocol.py: builtin functions and array
// properties (hover, definition, signature help, completion, semantic tokens,
// and inlay hints). Lines and columns are 1-based.
struct Badge { n: i64 }
fn format(out: u8[>..]&, b: Badge) { format(out, "#", b.n); }

thread_fn worker_fn(count: i64) { qput(count); }

fn pick_arr(xs: i64[>..<]&, a: i64, b: i64) -> i64[>..<]& { xs }

fn main() {
    var xs: i64[>..<] = [];
    xs.push(10);
    xs.resize(4, 0);
    var lim: i64[..4] = [];
    lim.push(1);
    let tid = thread_spawn(worker_fn, 7);
    thread_wait(tid);
    let img = xs.to_bytes();
    var buf: u8[>..] = [];
    xs.to_bytes(buf);
    let loaded, ok = from_bytes<i64[>..]>(img);
    let qv, qok = qpoll<i64>();
    let q1 = qget<i64>();
    let q2, q2ok = qget<i64>(5);
    format(buf, "n=", 1, Badge { n: 2 });
    buf.format("a", "b");
    pick_arr(xs, 1, 2).resize(3, 0);
    assert(xs.len > 0);
    print("x", 1, 2, lim.cap, tid, loaded.len, ok, qv, qok, q1, q2, q2ok);
    qlimit<i64>(8);
    let b0 = Badge { n: 3 };
    let bc = copy(b0);
    let def_i = default<i64>();
    print(bc.n + def_i);
    var lim_u8: u8[..16] = [];
    xs.to_bytes(lim_u8);
    push(xs, 5);
    resize(xs, 4, 0);
    xs.resize(2);
    thread_wait /* < */ (tid);
}

fn probe_buf() {
    var buf: u8[>..] = [];
    let c = buf.
}

fn probe_lim() {
    var lim: i64[..4] = [];
    let l = lim.
}

fn stopped_exemplar<T>(a: T) {
    let n: i64 = a;
    let s = n.len;
    n.push(1);
    n.no_such_method(1);
}

fn to_bytes(s: u8[:]) -> i64 { s.len }

fn slice_err(s: u8[:]) {
    let x = s.to_bytes(1);
}

fn probe_slice(s: u8[:]) {
    let m = s.
}

fn opt_arr_err(opt_buf: u8[>..]&?) {
    opt_buf.format("x");
}

fn pop(a: i64[4]&?) -> i64 { 0 }

fn opt_fixed_err(opt_fixed: i64[4]&?) {
    opt_fixed.pop();
}

fn unchecked_after_error() {
    var xs: i64[>..<] = [];
    let bad: i64 = "x";
    xs.resize(3, 0);
    xs.push(1);
}

struct FieldShadow { push: i64 }

fn field_shadow_err(fs: FieldShadow) {
    fs.push(1);
}

fn typeparam_shadow<push>(n: i64) {
    n.push(1);
}

fn call_tp_shadow() {
    typeparam_shadow<u8>(1);
}

fn local_fn_shadow() {
    fn push(x: i64, y: i64) -> i64 { x + y }
    let n: i64 = 1;
    let ok = n.push(2);
    let ok_free = push(1, 2);
    print(ok + ok_free);
}

fn local_fn_shadow_err() {
    fn push(x: i64, y: i64) -> i64 { x + y }
    let n: i64 = 1;
    let bad = n.push("wrong");
}

// lsp: error 47
// lsp: error 52
// lsp: error 64
// lsp: error 69
// lsp: error 72
// lsp: error 78
// lsp: error 83
// lsp: error 91
// lsp: error 95
// lsp: error 113
// lsp: hover 13:8 "builtin fn push(a, v: T) -> T&"
// lsp: hover 13:8 "push(i64[>..<], i64) -> i64&"
// lsp: hover 13:8 "Constructs element `v` in place"
// lsp: hover 29:15 "builtin .len: i64"
// lsp: hover 29:15 "The element count of the array or slice."
// lsp: hover 30:26 "builtin .cap: i64"
// lsp: hover 30:26 "The capacity of the limited array"
// lsp: hover 23:19 "builtin fn qpoll<T>() -> T, bool"
// lsp: hover 23:19 "qpoll<i64>() -> i64, bool"
// lsp: hover 22:22 "builtin fn from_bytes<A>(b: u8[:]) -> A, bool"
// lsp: hover 22:22 "from_bytes<i64[>..]>(u8[:]) -> i64[>..], bool"
// lsp: hover 24:14 "builtin fn qget<T>() -> T"
// lsp: hover 24:14 "qget<i64>() -> i64"
// lsp: hover 25:20 "builtin fn qget<T>(ms: i64) -> T, bool"
// lsp: hover 25:20 "qget<i64>(i64) -> i64, bool"
// lsp: hover 19:18 "builtin fn to_bytes(a) -> u8[>..]"
// lsp: hover 21:8 "builtin fn to_bytes(a, out)"
// lsp: hover 37:8 "builtin fn to_bytes(a, out)"
// lsp: hover 37:8 "to_bytes(i64[>..<], u8[..16])"
// lsp: hover 40:8 "builtin fn resize(a, n: i64)"
// lsp: hover 40:8 "Shrinks `a` to length `n`"
// lsp: hover 17:15 "builtin fn thread_spawn(worker, args...) -> i64"
// lsp: hover 17:15 "thread_spawn(worker_fn, i64) -> i64"
// lsp: hover 26:5 "builtin fn format(out, values...)"
// lsp: hover 27:9 "builtin fn format(out, values...)"
// lsp: hover 7:38 "builtin fn qput(v: T) -> bool"
// lsp: hover 7:38 "qput(i64) -> bool"
// lsp: hover 31:5 "builtin fn qlimit<T>(n: i64)"
// lsp: hover 31:5 "qlimit<i64>(i64)"
// lsp: hover 33:14 "builtin fn copy(x: T) -> T"
// lsp: hover 33:14 "copy(Badge) -> Badge"
// lsp: hover 34:17 "builtin fn default<T>() -> T"
// lsp: hover 34:17 "default<i64>() -> i64"
// lsp: hover 56:15 "Not checked: the exemplar instantiation ends before this code."
// lsp: hover 57:7 "builtin fn push(a, v: T) -> T&"
// lsp: hover 58:7 "Not checked: the exemplar instantiation ends before this code."
// lsp: hover 78:15 "builtin fn pop(a) -> T"
// lsp: hover 84:8 "builtin fn resize(a, n: i64)"
// lsp: hover 91:8 ".push"
// lsp: hover 95:7 ".push"
// lsp: hover 113:17 "fn push(x: i64, y: i64) -> i64"
// lsp: definition 13:8 -> none
// lsp: definition 17:15 -> none
// lsp: definition 26:5 -> none
// lsp: definition 27:9 -> none
// lsp: definition 72:13 -> none
// lsp: definition 78:15 -> none
// lsp: definition 91:8 -> none
// lsp: definition 95:7 -> none
// lsp: definition 113:17 -> 111:8
// lsp: rename 72:13 fmt2 -> error
// lsp: rename 78:15 pop2 -> error
// lsp: signature 14:15 "builtin fn resize(a, n: i64, v: T)" 1 "n: i64"
// lsp: signature 14:18 "builtin fn resize(a, n: i64, v: T)" 2 "v: T"
// lsp: signature 28:34 "builtin fn resize(a, n: i64, v: T)" 2 "v: T"
// lsp: signature 24:24 "builtin fn qget<T>() -> T" 0
// lsp: signature 25:30 "builtin fn qget<T>(ms: i64) -> T, bool" 0 "ms: i64"
// lsp: signature 26:25 "builtin fn format(out, values...)" 1 "values..."
// lsp: signature 27:21 "builtin fn format(out, values...)" 1 "values..."
// lsp: signature 30:22 "builtin fn print(values...)" 0 "values..."
// lsp: signature 41:26 "builtin fn thread_wait(id: i64) -> bool" 0 "id: i64"
// lsp: signature 57:12 "builtin fn push(a, v: T) -> T&" 1 "v: T"
// lsp: signature 58:22 none
// lsp: signature 64:24 "fn to_bytes(s: u8[:]) -> i64" 1 only
// lsp: signature 72:20 "builtin fn format(out, values...)" 1 "values..."
// lsp: signature 78:19 "builtin fn pop(a) -> T" 1 only
// lsp: signature 84:15 "builtin fn resize(a, n: i64, v: T)" 1 "n: i64"
// lsp: signature 84:18 "builtin fn resize(a, n: i64, v: T)" 2 "v: T"
// lsp: signature 85:13 "builtin fn push(a, v: T) -> T&" 1 "v: T"
// lsp: signature 91:13 none
// lsp: signature 95:12 none
// lsp: signature 105:21 "fn push(x: i64, y: i64) -> i64" 1 only
// lsp: signature 106:24 "fn push(x: i64, y: i64) -> i64" 0
// lsp: signature 113:22 "fn push(x: i64, y: i64) -> i64" 1 only
// lsp: complete 46:17 has push "builtin fn push(a, v: T) -> T&" "Constructs element `v` in place"
// lsp: complete 46:17 has format "builtin fn format(out, values...)" "Appends the arguments' text"
// lsp: complete 51:17 has cap "builtin .cap: i64" "capacity"
// lsp: complete 14:5 has from_bytes "builtin fn from_bytes<A>(b: u8[:]) -> A, bool" "Verifies untrusted image"
// lsp: complete 68:15 has to_bytes "fn to_bytes(s: u8[:]) -> i64"
// lsp: token 30:5 function+defaultLibrary
// lsp: token 13:8 method+defaultLibrary
// lsp: token 29:15 property+readonly+defaultLibrary
// lsp: token 56:15 property+none
// lsp: token 57:7 method+defaultLibrary
// lsp: token 58:7 method+none
// lsp: token 78:15 method+defaultLibrary
// lsp: token 84:8 method+defaultLibrary
// lsp: token 91:8 method+none
// lsp: token 95:7 method+none
// lsp: token 113:17 method+none
// lsp: hints 13 0
// lsp: hint 14:15 "n:"
// lsp: hint 14:18 "v:"
// lsp: hint 17:28 "worker:"
// lsp: hint 17:39 "count:"
// lsp: hints 18 0
// lsp: hints 29 0
// lsp: hints 30 0
// lsp: hints 38 0
// lsp: hints 39 2
// lsp: hint 39:16 "n:"
// lsp: hint 39:19 "v:"
