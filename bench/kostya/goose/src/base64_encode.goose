// Base64::Encode: encodes a string of `size` 'a's per run.

namespace base64_encode;

import std;
import bench;
import helper;
import base64;

// The first four bytes of `s` and "...", or all of `s` if it is no longer.
fn preview(out: u8[>..]&, s: u8[:]) {
    if s.len > 4 { format(out, s[..4], "..."); } else { out.append(s); }
}

fn run(b: Bench&) -> u32 {
    let n = config_i64(b.name, "size");
    var text: u8[>..] = [];
    text.resize(n, 'a');
    var encoded: u8[>..] = [];
    base64::encode(encoded, text);
    var result: u32 = 0;
    measure(b) {
        encoded.clear();
        base64::encode(encoded, text);
        result += encoded.len as! u32;
    };
    var message: u8[>..] = [];
    message.append("encode ");
    preview(message, text);
    message.append(" to ");
    preview(message, encoded);
    format(message, ": ", result);
    return checksum_str(message);
}
