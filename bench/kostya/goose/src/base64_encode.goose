// Base64::Encode: encodes a string of `size` 'a's per run.

namespace base64_encode;

import std;
import bench;
import helper;
import base64;

// Rust's str::repeat: `s` n times over.
fn repeat(s: u8[:], n: i64) -> u8[>..] {
    var r: u8[>..] = [];
    for i in n { r.append(s); }
    return r;
}

// The returning form of the library's encoder, as base64_simd's
// encode_to_string: the encoding of `src` as a new string.
fn encode_to_string(src: const u8[:]) -> u8[>..] {
    var out: u8[>..] = [];
    base64::encode(out, src);
    return out;
}

// The first four bytes of `s` and "...", or all of `s` if it is no longer.
fn preview(out: u8[>..]&, s: u8[:]) {
    if s.len > 4 { format(out, s[..4], "..."); } else { out.append(s); }
}

// The text is made once and never changes size, so it is a fixed-length
// string ahead of the encoding, the growable field, which has to be last.
struct Base64Encode { result_val: u32, str_data: u8[], str2_encoded: u8[>..] }

fn new_base64_encode() -> Base64Encode {
    let n = config_i64("Base64::Encode", "size");
    var s = Base64Encode { result_val: 0, str_data: repeat("a", n), str2_encoded: [] };
    s.str2_encoded = encode_to_string(s.str_data);
    return s;
}

fn prepare(s: Base64Encode&) {}

fn run(s: Base64Encode&, iteration_id: i64) {
    s.str2_encoded = encode_to_string(s.str_data);
    s.result_val += s.str2_encoded.len as! u32;
}

fn checksum(s: Base64Encode&) -> u32 {
    var message: u8[>..] = [];
    message.append("encode ");
    preview(message, s.str_data);
    message.append(" to ");
    preview(message, s.str2_encoded);
    format(message, ": ", s.result_val);
    return checksum_str(message);
}
