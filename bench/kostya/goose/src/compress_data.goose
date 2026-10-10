// The input every Compress benchmark encodes: the configured number of bytes
// of a repeating pattern.

import std;

fn generate_test_data(size: i64) -> u8[>..] {
    let pattern = "ABRACADABRA";
    var data: u8[>..] = [];
    for i in size { data.push(pattern[i % pattern.len]); }
    return data;
}
