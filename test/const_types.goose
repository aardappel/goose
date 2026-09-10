// `const T&` and `const T[:]` (§9.5): read-only through the type. A read-only
// reference or slice -- a literal, a slice of a let, a const slot's contents --
// lands only in slots declared const; parameters and results take either and
// are checked read-only per instantiation; adding const is implicit.
import std;

struct Named { name: const u8[:], n: i64 }
struct View { bytes: const u8[:] }
struct Buf { bytes: u8[:] }
struct Link { r: const i64& }
const LIMIT = 3;
let NAMES: const u8[:][3] = ["a", "b", "c"];

fn count(xs: u8[:]) -> i64 { return xs.len; }
fn first(xs: const u8[:]) -> u8 { return xs[0]; }
fn head(xs: u8[:]) -> u8[:] { return xs[..1]; }
fn pick(s: Named&) -> const u8[:] { return s.name; }
fn bump(b: Buf&) { b.bytes[0] = 'H'; }

thread_fn worker() {
    let rec = Named { name: "worker", n: LIMIT };
    var l = Link { r: LIMIT };
    print(rec.name, " ", rec.n, " ", l.r);
}

fn main_const_types() {
    // Literals in const slots, read back read-only.
    var recs: Named[>..] = [];
    recs.push(Named { name: "one", n: 1 });
    recs.push(Named { name: "two", n: 2 });
    for r in recs { print(r.name, " ", r.n); }
    print(pick(recs[0]) == "one", " ", first("xyz"), " ", count("four"));
    // A writable slice fits a const slot too, and reads back read-only.
    var text: u8[>..] = "hello";
    let v = View { bytes: text[..] };
    print(v.bytes);
    // A writable slot takes only writable slices, and writes through them.
    var b = Buf { bytes: text[..] };
    bump(b);
    print(text);
    // Constness is inferred through calls and results.
    let h = head(text[..]);
    h[0] = 'J';
    print(text, " ", head("const"));
    // A slice of a const value is a const slice; copies of literals are plain arrays.
    const fixed: u8[3] = "abc";
    let fs = fixed[..];
    var copy: u8[3] = "abc";
    copy[0] = 'X';
    print(fs, " ", copy);
    // The parts split returns are read-only views of either kind of input.
    let parts = split("x,y", ',');
    print(parts, " ", split(text[..], 'l'));
    // Branches and comparisons join constness.
    let pick2 = if text.len > 3 { "long" } else { text[..] };
    print(pick2, " ", pick2 == text[..], " ", NAMES[1]);
    thread_wait(thread_spawn(worker));
    print("abc");
}

fn main() { main_const_types(); }
