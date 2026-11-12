// lsp: fixture for test/lsp_protocol.py's builtin protocol checks.
// The trailing markers locate requests without coupling them to line numbers.
import std;

struct BuiltinCollision { push: i64 }

thread_fn builtin_worker(a: i64, b: i64, c: i64, d: i64) {
    print(a, b, c, d);
}

// default<T>() stops these exemplars before the member accesses, so those
// occurrences have no checked receiver type.
fn stopped_field<T>(x: BuiltinCollision) {
    let stop = default<T>();
    let value = x.push; // stopped_field
}

fn stopped_scalar<T>(x: i64) {
    let stop = default<T>();
    x.push(1); // scalar_collision
}

fn stopped_array<T>(xs: i64[>..]&) {
    let stop = default<T>();
    xs.push(1); // unchecked_push
    let count = xs.len; // unchecked_len
    let method = xs.push; // method_as_field
    let count_call = xs.len(); // property_as_call
}

fn opaque_field<T>(x: T) {
    let value = x.push; // opaque_field
}

fn opaque_call<T>(x: T) {
    x.push(1); // opaque_call
}

fn partial_push<T>(xs: i64[>..]&) {
    xs.push(default<T>()); // partial_push
}

fn main() {
    let threads = hardware_threads(); // hardware
    let row = src_line(); // source_line
    let sent = qput(1); // queue_put
    let blocking = qget<i64>(); // queue_blocking
    let timed, ready = qget<i64>(100); // queue_timed
    var xs: i64[>..] = [];
    xs.push(1); // member_push
    let last = xs.pop(); // member_pop
    xs.resize(4, 0); // member_resize
    xs.resize(2); // member_resize_short
    let count = xs.len; // member_len
    let image = xs.to_bytes(); // member_bytes
    var out: u8[>..] = [];
    xs.to_bytes(out); // member_bytes_out
    let global_image = to_bytes(xs); // global_bytes
    to_bytes(xs, out); // global_bytes_out
    let decoded, valid = from_bytes<i64[]>(image[..]); // decode
    print(); // print_empty
    print(threads, row, sent, blocking, timed, ready, last, count, valid); // print_many
    let id = thread_spawn(builtin_worker, 1, 2, 3, 4); // spawn_many
    let contained = thread_spawn_contained(builtin_worker, 1, 2, 3, 4); // contained_many
    let joined = thread_wait(id); // wait
    thread_wait(contained);
    let collision = BuiltinCollision { push: 7 };
    print(collision.push); // checked_field
}
