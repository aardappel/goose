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
    run_parallel();
}

// A thread_pool, whose apply and close are builtin members, and the builtins
// that run a worker function on the arguments after its name: pool.apply,
// whose worker takes its chunk (and its index) first, and thread_spawn.

fn scale_chunk(chunk: i64[:], idx: i64, by: i64, offset: i64) {
    for &x in chunk { x = x * by + idx + offset; }
}

fn shift_chunk(chunk: i64[:], by: i64) {
    for &x in chunk { x += by; }
}

thread_fn report(n: i64, total: i64) { print(n + total); }

fn shift_all(pool: thread_pool&, xs: i64[:], step: i64) {
    pool.apply(chunks(xs, 2), shift_chunk, step);
}

fn run_parallel() {
    var pool = thread_pool(2);
    var xs: i64[>..] = [1, 2, 3, 4];
    let offset = 1;
    pool.apply(chunks(xs, 2), scale_chunk, 3, offset);
    shift_all(pool, xs, 4);
    let first = thread_spawn(report, xs.len, 10);
    let second = thread_spawn_contained(report, 2, offset);
    thread_wait(first);
    thread_wait(second);
    print(xs);
    pool.close();
}

// pool.apply_contained, which may take a message buffer ahead of the name.
fn check_chunk(chunk: i64[:], idx: i64, limit: i64, scale: i64) {
    for &x in chunk { assert(x < limit); x = x * scale + idx; }
}

fn try_parallel(pool: thread_pool&, xs: i64[:], why: u8[>..]&) -> bool {
    let ok = pool.apply_contained(chunks(xs, 2), why, check_chunk, 100, 2);
    ok && pool.apply_contained(chunks(xs, 1), shift_chunk, 3)
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
// lsp: complete 28:18 lacks apply
// lsp: complete 57:10 has apply "apply(chunks(array, size_or_offsets), fn, args...)"
// lsp: complete 57:10 has shift_all
// lsp: complete 57:10 lacks push
// lsp: complete 57:10 lacks alloc_index
// lsp: complete 50:10 has apply "apply(chunks(array, size_or_offsets), fn, args...)"
// lsp: complete 57:10 has close "close()"
// lsp: complete 50:10 has close "close()"
// lsp: complete 64:10 has close "close()"
// lsp: complete 28:18 lacks close
// lsp: complete 59:41 lacks close
// lsp: complete 59:41 has len
// lsp: complete 59:41 lacks apply
// lsp: hint 54:13 ": thread_pool"
// lsp: hints 57 1
// lsp: hint 57:44 "by:"
// lsp: hints 50 1
// lsp: hint 50:44 "by:"
// lsp: hints 59 3
// lsp: hint 59:38 "n:"
// lsp: hint 59:46 "total:"
// lsp: hints 60 3
// lsp: hint 60:49 "n:"
// lsp: hint 60:52 "total:"
// lsp: hover 54:9 "var pool: thread_pool"
// lsp: hover 49:14 "parameter pool: thread_pool&"
// lsp: hover 57:10 "builtin .apply"
// lsp: hover 50:10 "builtin .apply"
// lsp: hover 64:10 "builtin .close"
// lsp: hover 59:17 "builtin thread_spawn"
// lsp: hover 60:18 "builtin thread_spawn_contained"
// lsp: hover 57:31 "fn scale_chunk(chunk: i64[:], idx: i64, by: i64, offset: i64)"
// lsp: definition 57:31 -> 39:4
// lsp: definition 57:10 -> none
// lsp: definition 64:10 -> none
// lsp: definition 59:17 -> none
// lsp: references 39:4 2
// lsp: references 47:11 3
// lsp: rename 47:11 reporter -> 3
// lsp: rename 57:10 renamed -> error
// lsp: complete 73:19 has apply_contained "apply_contained(chunks(array, size_or_offsets), [msg, ]fn, args...) -> bool"
// lsp: complete 57:10 has apply_contained
// lsp: complete 73:19 has apply
// lsp: complete 28:18 lacks apply_contained
// lsp: complete 59:41 lacks apply_contained
// lsp: hover 73:19 "builtin .apply_contained -> bool"
// lsp: hover 74:16 "builtin .apply_contained -> bool"
// lsp: definition 73:19 -> none
// lsp: hover 73:55 "fn check_chunk(chunk: i64[:], idx: i64, limit: i64, scale: i64)"
// lsp: definition 73:55 -> 68:4
// lsp: hint 73:11 ": bool"
// lsp: hint 73:68 "limit:"
// lsp: hint 73:73 "scale:"
// lsp: hints 73 3
// lsp: hint 74:60 "by:"
// lsp: hints 74 1
