// lsp: fixture for test/lsp_protocol.py: root structs and the stdlib pools,
// whose functions are generic over the struct they take first, so that a
// value's completions are how their UFCS calls are found. Lines and columns
// are 1-based.
import std;
import slot_pool;
import slice_pool;

struct Box<T> { v: T }

fn get_v<T>(b: Box<T>&) -> T { b.v }

// Values kept together.
root struct Arena<T> {
    // The values, in the order added.
    xs: T[>..] = [],
}

fn first_of<T>(a: Arena<T>&) -> T { a.xs[0] }

fn mine(p: slot_pool<i64>&) -> i64 { p.items.len }

fn main() {
    var b = Box<i64> { 1 };
    var a = Arena<i64> { .. };
    a.xs.push(2);
    var pool = slot_pool<i64> {};
    let i = pool.alloc_index(3);
    var bytes = slice_pool<u8> {};
    let s = bytes.alloc_slice(4);
    print(b.get_v() + a.first_of() + mine(pool) + i + s.len);
}

// lsp: hover 14:13 "root struct Arena<T>"
// lsp: hover 25:13 "root struct Arena<T>"
// lsp: hover 27:16 "root struct slot_pool<T>"
// lsp: hover 29:17 "root struct slice_pool<T>"
// lsp: hover 28:18 "fn alloc_index<T>(pool: slot_pool<T>&, value: T) -> i64"
// lsp: hover 30:19 "fn alloc_slice<T>(p: slice_pool<T>&, n: i64) -> T[:]"
// lsp: definition 31:25 -> 19:4
// lsp: definition 31:13 -> 11:4
// lsp: definition 28:18 -> slot_pool.goose
// lsp: definition 30:19 -> slice_pool.goose
// lsp: definition 27:16 -> slot_pool.goose
// lsp: complete 31:13 has v
// lsp: complete 31:13 has get_v
// lsp: complete 31:13 lacks first_of
// lsp: complete 31:13 lacks alloc_ref
// lsp: complete 31:25 has xs "xs: i64[>..]"
// lsp: complete 31:25 has first_of
// lsp: complete 31:25 lacks get_v
// lsp: complete 31:25 lacks recycle_all
// lsp: complete 28:18 has items "items: i64[>..]"
// lsp: complete 28:18 has free_slots
// lsp: complete 28:18 has mine
// lsp: complete 28:18 has alloc_ref
// lsp: complete 28:18 has alloc_index
// lsp: complete 28:18 has alloc_with
// lsp: complete 28:18 has alloc_index_with
// lsp: complete 28:18 has free
// lsp: complete 28:18 has free_ref
// lsp: complete 28:18 has recycle_all
// lsp: complete 28:18 lacks alloc_slice
// lsp: complete 28:18 lacks first_of
// lsp: complete 30:19 has items "items: u8[>..]"
// lsp: complete 30:19 has alloc_slice
// lsp: complete 30:19 has free_slice
// lsp: complete 30:19 has alloc_copy
// lsp: complete 30:19 has realloc_slice
// lsp: complete 30:19 has recycle_all
// lsp: complete 30:19 lacks alloc_ref
// lsp: complete 30:19 lacks mine
// lsp: complete 30:19 lacks defaults
// lsp: complete 30:19 lacks finish
