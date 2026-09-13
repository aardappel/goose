// The library side of the namespaces test (docs/design/namespaces.md): every
// declaration here is image::<name>, reached unqualified from this file and
// from any other `namespace image;` file, and as image::<name> elsewhere.
namespace image;

import std;
import .geometry;

struct Pixel { r: u8, g: u8, b: u8 }

fn brightness(p: Pixel) -> i64 { p.r + p.g + p.b }

// The root file has a global `brightness` too. Inside this namespace the
// bare name is image::brightness, and `::brightness` reaches the global one.
fn twice_bright(p: Pixel) -> i64 { ::brightness(brightness(p)) }

// A generic body resolves names where it is defined: whoever instantiates
// this, `brightness` is image::brightness.
fn scaled<T>(p: T, k: i64) -> i64 { brightness(p) * k }

// A custom format hook (§3.7), found for an image::Pixel from any namespace.
// The `format` it calls is the builtin: this namespace's own `format` set
// has no overload for those arguments, and a builtin takes the calls a user
// set of its name declines.
fn format(out: u8[>..]&, p: Pixel) { format(out, "#", p.r, "/", p.g, "/", p.b); }

enum Shape { Circle { r: f64 }, Square { s: f64 }, Empty }

// Case functions dispatch within one namespace's overload set.
fn area(s: Shape.Circle&) -> f64 { 3.0 * s.r * s.r }
fn area(s: Shape.Square&) -> f64 { s.s * s.s }
fn area(s: Shape.Empty&) -> f64 { 0.0 }

// A key type for the global dictionary module. Overload sets do not merge
// across namespaces: the integer `hash` overloads are global, and this
// namespace's own `hash` set would hide them from a bare call.
struct Key { a: i64, b: i64 }
fn hash(k: Key) -> u64 { hash_combine(::hash(k.a), ::hash(k.b)) }

// A pool for `in pool` references (§3.9), named image::pool from the root.
struct Node { v: i64, next: Node&<u16 in pool>? }
var pool: Node[>..] = [];
fn push_node(v: i64) { pool.push(Node { v: v }); }

// `return ... from parse` targets the parse this namespace resolves the name
// to, image::parse, however many other functions called `parse` sit between:
// geometry::parse below it, the root's global parse above it.
fn digit(c: u8) -> i64 {
    if c < '0' || c > '9' { return -1 from parse; }
    c - '0'
}
fn parse(s: u8[:]) -> i64 { geometry::parse(s) + 1000 }

thread_fn worker(x: i64) { qput(x + 1); }

// Ignored when this file is imported (§11.1): it is image::main, not the entry.
fn main() { print("image main"); }
