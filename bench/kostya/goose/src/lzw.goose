// Compress::LZWEncode and Compress::LZWDecode: Lempel-Ziv-Welch with an
// unbounded dictionary of strings and 16-bit big-endian output codes.
//
// Goose has no heap-allocated string type: a string is a byte array, and a
// string made at run time is built at the end of a byte array the function
// owns (`strings`, freed when it returns) and handled as a slice of it, which
// is what the dictionaries hold.

namespace lzw;

import std;
import dictionary;
import bench;
import compress_data;

struct LZWResult { data: u8[>..] }

// A new string with the bytes of `s`.
fn new_string(strings: u8[>..]&, s: u8[:]) -> const u8[:] {
    let start = strings.len;
    strings.append(s);
    return strings[start..];
}

// The new string `a + b`.
fn concat(strings: u8[>..]&, a: u8[:], b: u8[:]) -> const u8[:] {
    let start = strings.len;
    strings.append(a);
    strings.append(b);
    return strings[start..];
}

// --- encoding -----------------------------------------------------------------

fn lzw_encode(input: u8[:]) -> LZWResult {
    if input.len == 0 {
        return LZWResult { data: [] };
    }

    var strings: u8[>..] = [];
    var dict = dictionary<const u8[:], i32> {};
    dict.reserve(4096);
    for i: i32 in 0..256 {
        dict.insert(new_string(strings, [i as! u8]), i);
    }

    var next_code: i32 = 256;

    var result = LZWResult { data: [] };

    var current = new_string(strings, input[0..1]);

    for i in 1..input.len {
        let next_char_str = new_string(strings, input[i..i + 1]);
        let new_str = concat(strings, current, next_char_str);

        if dict.contains(new_str) {
            current = new_str;
        } else {
            let code = dict.get(current);
            assert(code);
            result.data.push(((code >> 8) & 0xFF) as! u8);
            result.data.push((code & 0xFF) as! u8);

            dict.insert(new_str, next_code);
            next_code += 1;
            current = next_char_str;
        }
    }

    let code = dict.get(current);
    assert(code);
    result.data.push(((code >> 8) & 0xFF) as! u8);
    result.data.push((code & 0xFF) as! u8);

    return result;
}

fn run_encode(b: Bench&) -> u32 {
    let test_data = ::generate_test_data(config_i64(b.name, "size"));
    var encoded = LZWResult { data: [] };
    var result_val: u32 = 0;
    measure(b) {
        encoded = lzw_encode(test_data);
        result_val += encoded.data.len as! u32;
    };
    return result_val;
}

// --- decoding -----------------------------------------------------------------

fn lzw_decode(encoded: LZWResult&) -> u8[>..] {
    if encoded.data.len == 0 {
        return [];
    }

    var strings: u8[>..] = [];
    var dict: (const u8[:])[>..] = [];
    for i in 256 {
        dict.push(new_string(strings, [i as! u8]));
    }

    var result: u8[>..] = [];
    let data = encoded.data;
    var pos = 0;

    let high = data[pos] as u16;
    let low = data[pos + 1] as u16;
    let old_code = ((high as i64) << 8) | low;
    pos += 2;

    var old_str = dict[old_code];
    result.append(old_str);

    var next_code = 256;

    while pos < data.len {
        let high = data[pos] as u16;
        let low = data[pos + 1] as u16;
        let new_code = ((high as i64) << 8) | low;
        pos += 2;

        let new_str = if new_code < dict.len {
            dict[new_code]
        } else if new_code == next_code {
            concat(strings, old_str, old_str[0..1])
        } else {
            return [];
        };

        result.append(new_str);

        dict.push(concat(strings, old_str, new_str[0..1]));
        next_code += 1;

        old_str = new_str;
    }

    return result;
}

fn run_decode(b: Bench&) -> u32 {
    let test_data = ::generate_test_data(config_i64(b.name, "size"));
    // LZWEncode's prepare and run(0).
    let encoded = lzw_encode(test_data);
    var decoded: u8[>..] = [];
    var result_val: u32 = 0;
    measure(b) {
        decoded = lzw_decode(encoded);
        result_val += decoded.len as! u32;
    };
    if decoded == test_data { result_val += 100000; }
    return result_val;
}
