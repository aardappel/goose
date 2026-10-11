// The input every Compress benchmark encodes: the configured number of bytes
// of a repeating pattern.
//
// The Compress benchmarks keep more arrays than a Goose struct can own: a
// struct holds at most one growable array, as its last field. Each benchmark
// struct owns the array its run makes, an encoder's result or a decoder's
// decoded bytes, and refers to the others, which the dispatcher (main.goose)
// owns as locals while the benchmark runs: the test data, and the encoded
// result a decoder's prepare takes from its encoder. The test data is
// grow-shrink (`[>..<]`), the other arrays grow-only: an encoder's run reads
// the test data while it replaces its result, and the value assigned to an
// array may read another array only where its type shows the two are
// different. (Prepare also replaces the test data whole through the
// reference, which the compiler does not yet accept for a grow-only array
// while the struct is still in use.)

import std;

fn generate_test_data(size: i64) -> u8[>..] {
    let pattern = "ABRACADABRA";
    var data: u8[>..] = [];
    for i in size { data.push(pattern[i % pattern.len]); }
    return data;
}
