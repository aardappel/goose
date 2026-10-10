// Compress::LZWEncode and Compress::LZWDecode: Lempel-Ziv-Welch with an
// unbounded dictionary of byte strings and 16-bit big-endian output codes.
//
// Every string the encoder looks up is a run of its input, so its dictionary
// is keyed by views of the input. The decoder builds each new string, end to
// end with the others in one byte array, and its table holds views of them.

namespace lzw;

import std;
import dictionary;
import bench;
import compress_data;

// The 256 one-byte strings the dictionaries start with are views of this.
fn all_bytes() -> u8[256] {
    var bytes: u8[256] = [0; 256];
    for i in 256 { bytes[i] = i as! u8; }
    return bytes;
}

fn push_code(out: u8[>..]&, code: i64) {
    out.push(((code >> 8) & 0xFF) as! u8);
    out.push((code & 0xFF) as! u8);
}

// --- encoding -----------------------------------------------------------------

fn encode(input: u8[:]) -> u8[>..] {
    var result: u8[>..] = [];
    if input.len == 0 { return result; }
    let bytes = all_bytes();
    var dict = dictionary<const u8[:], i64> {};
    dict.reserve(4096);
    for i in 256 { dict.insert(bytes[i..i + 1], i); }
    var next_code = 256;

    // The current string is input[start..i]; extending it by input[i] is
    // input[start..i + 1].
    var start = 0;
    for i in 1..input.len {
        let new_str = input[start..i + 1];
        if !dict.contains(new_str) {
            let code = dict.get(input[start..i]);
            assert(code);
            push_code(result, code);
            dict.insert(new_str, next_code);
            next_code += 1;
            start = i;
        }
    }
    let code = dict.get(input[start..]);
    assert(code);
    push_code(result, code);
    return result;
}

fn run_encode(b: Bench&) -> u32 {
    let test_data = ::generate_test_data(config_i64(b.name, "size"));
    var result: u32 = 0;
    measure(b) {
        let encoded = encode(test_data);
        result += encoded.len as! u32;
    };
    return result;
}

// --- decoding -----------------------------------------------------------------

// `s` followed by `c`, built at the end of `strings`.
fn concat(strings: u8[>..]&, s: u8[:], c: u8) -> const u8[:] {
    let start = strings.len;
    strings.append(s);
    strings.push(c);
    return strings[start..];
}

fn decode(data: u8[:]) -> u8[>..] {
    var result: u8[>..] = [];
    if data.len == 0 { return result; }
    let bytes = all_bytes();
    var strings: u8[>..] = [];
    var dict: (const u8[:])[>..] = [];
    for i in 256 { dict.push(bytes[i..i + 1]); }

    fn code_at(pos: i64) -> i64 { ((data[pos] as i64) << 8) | data[pos + 1] }

    var old_str = dict[code_at(0)];
    result.append(old_str);
    var pos = 2;
    var valid = true;
    while pos < data.len {
        let new_code = code_at(pos);
        pos += 2;
        if new_code > dict.len {
            valid = false;
            break;
        }
        // A code equal to dict.len is the one this step defines: the
        // previous string plus its own first byte.
        let new_str = if new_code < dict.len { dict[new_code] }
                      else { concat(strings, old_str, old_str[0]) };
        result.append(new_str);
        dict.push(concat(strings, old_str, new_str[0]));
        old_str = new_str;
    }
    // A code no encoder could have written decodes to nothing.
    if !valid { result.clear(); }
    return result;
}

fn run_decode(b: Bench&) -> u32 {
    let test_data = ::generate_test_data(config_i64(b.name, "size"));
    let encoded = encode(test_data);
    var decoded: u8[>..] = [];
    var result: u32 = 0;
    measure(b) {
        decoded = decode(encoded);
        result += decoded.len as! u32;
    };
    if decoded == test_data { result += 100000; }
    return result;
}
