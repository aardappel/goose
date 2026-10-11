// Base64::Decode: decodes the encoding of a string of `size` 'a's per run.

namespace base64_decode;

import std;
import bench;
import helper;
import base64;
import base64_encode;

// The returning form of the library's decoder, as base64_simd's
// decode_to_vec(..).unwrap(): the bytes `src` encodes, as a new array.
fn decode_to_vec(src: const u8[:]) -> u8[>..] {
    var out: u8[>..] = [];
    if !base64::decode(out, src) { abort("Base64::Decode: invalid input"); }
    return out;
}

// The encoding is made once and never changes size, so it is a fixed-length
// string ahead of the decoded bytes, the growable field, which has to be
// last.
struct Base64Decode { result_val: u32, str2_encoded: u8[], str3_decoded: u8[>..] }

fn new_base64_decode() -> Base64Decode {
    let n = config_i64("Base64::Decode", "size");
    let str_data = base64_encode::repeat("a", n);
    var s = Base64Decode {
        result_val: 0,
        str2_encoded: base64_encode::encode_to_string(str_data),
        str3_decoded: [],
    };
    s.str3_decoded = decode_to_vec(s.str2_encoded);
    return s;
}

fn prepare(s: Base64Decode&) {}

fn run(s: Base64Decode&, iteration_id: i64) {
    s.str3_decoded = decode_to_vec(s.str2_encoded);
    s.result_val += s.str3_decoded.len as! u32;
}

fn checksum(s: Base64Decode&) -> u32 {
    var message: u8[>..] = [];
    message.append("decode ");
    base64_encode::preview(message, s.str2_encoded);
    format(message, " to ", s.str3_decoded[..4], "...: ", s.result_val);
    return checksum_str(message);
}
