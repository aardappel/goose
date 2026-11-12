// lsp: fixture for test/lsp_protocol.py's builtin protocol checks.
// Trailing markers locate protocol queries.
import std;

struct BuiltinCollision { push: i64 }

thread_fn builtin_worker(a: i64, b: i64, c: i64, d: i64) {
    print(a, b, c, d);
}

// default<T>() stops checking before these member accesses.
fn stopped_field<T>(x: BuiltinCollision) {
    let stop = default<T>();
    let value = x.push; // stopped_field
}

fn stopped_array<T>(xs: i64[>..]&) {
    let stop = default<T>();
    xs.push(1); // unchecked_push
    let count = xs.len; // unchecked_len
}

fn incompatible_scalar<T>(x: i64) {
    x.push(1); // scalar_collision
}

fn method_as_field<T>(xs: i64[>..]&) {
    let method = xs.push; // method_as_field
}

fn property_as_call<T>(xs: i64[>..]&) {
    let count = xs.len(); // property_as_call
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

fn excess_args<T>(xs: i64[>..]&, out: u8[>..]&) {
    to_bytes(xs, out, out); // bytes_over_arity
}

fn main() {
    let threads = hardware_threads(); // hardware
    let row = src_line(); // source_line
    let sent = qput(1); // queue_put
    let blocking = qget<i64>(); // queue_blocking
    let timed, ready = qget<i64>(100); // queue_timed
    var xs: i64[>..] = [];
    append(xs, [2]); // global_append
    xs.push(1); // member_push
    let last = xs.pop(); // member_pop
    xs.resize(4, 0); // member_resize
    xs.resize(2); // member_resize_short
    let count = xs.len; // member_len
    let image = xs.to_bytes(); // member_bytes
    var out: u8[>..] = [];
    xs.to_bytes(out); // member_bytes_out
    let global_image = to_bytes(xs); // global_bytes
    let pending_image = to_bytes(xs, ); // bytes_trailing_comma
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
