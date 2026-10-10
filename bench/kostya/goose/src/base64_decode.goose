// Base64::Decode: decodes the encoding of a string of `size` 'a's per run.

namespace base64_decode;

import std;
import bench;
import helper;
import base64;
import base64_encode;

fn run(b: Bench&) -> u32 {
    let n = config_i64(b.name, "size");
    var text: u8[>..] = [];
    text.resize(n, 'a');
    var encoded: u8[>..] = [];
    base64::encode(encoded, text);
    var decoded: u8[>..] = [];
    if !base64::decode(decoded, encoded) { abort("Base64::Decode: invalid input"); }
    var result: u32 = 0;
    measure(b) {
        decoded.clear();
        if !base64::decode(decoded, encoded) { abort("Base64::Decode: invalid input"); }
        result += decoded.len as! u32;
    };
    var message: u8[>..] = [];
    message.append("decode ");
    base64_encode::preview(message, encoded);
    format(message, " to ", decoded[..4], "...: ", result);
    return checksum_str(message);
}
