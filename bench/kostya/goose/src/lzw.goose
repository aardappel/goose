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

fn new_lzw_result() -> LZWResult { LZWResult { data: [] } }

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

// The encoder builds every phrase as a new string, as the other
// implementations do, for the sake of comparing like with like. This is not
// idiomatic Goose: every phrase is a substring of the input, so we would key
// the dictionary by slices of the input itself and build no strings at all,
// which runs LZWEncode about 1.8x faster.
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

// The test data is the caller's (see compress_data).
struct LZWEncode { size_val: i64, result_val: u32, test_data: u8[>..<]&, encoded: LZWResult }

fn new_lzw_encode(test_data: u8[>..<]&) -> LZWEncode {
    LZWEncode {
        size_val: config_i64("Compress::LZWEncode", "size"),
        result_val: 0,
        test_data: test_data,
        encoded: new_lzw_result(),
    }
}

fn prepare(s: LZWEncode&) { s.test_data = ::generate_test_data(s.size_val); }

fn run(s: LZWEncode&, iteration_id: i64) {
    s.encoded = lzw_encode(s.test_data);
    s.result_val += s.encoded.data.len as! u32;
}

fn checksum(s: LZWEncode&) -> u32 { s.result_val }

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

// The test data and the encoded result are the caller's (see compress_data).
struct LZWDecode {
    size_val: i64,
    result_val: u32,
    test_data: u8[>..<]&,
    encoded: LZWResult&,
    decoded: u8[>..],
}

fn new_lzw_decode(test_data: u8[>..<]&, encoded: LZWResult&) -> LZWDecode {
    LZWDecode {
        size_val: config_i64("Compress::LZWDecode", "size"),
        result_val: 0,
        test_data: test_data,
        encoded: encoded,
        decoded: [],
    }
}

fn prepare(s: LZWDecode&) {
    s.test_data = ::generate_test_data(s.size_val);

    var encoder_test_data: u8[>..<] = [];
    var encoder = new_lzw_encode(encoder_test_data);
    encoder.size_val = s.size_val;
    encoder.prepare();
    encoder.run(0);

    // Goose has no move for a growable array: the encoder's result is copied.
    s.encoded = copy(encoder.encoded);
}

fn run(s: LZWDecode&, iteration_id: i64) {
    s.decoded = lzw_decode(s.encoded);
    s.result_val += s.decoded.len as! u32;
}

fn checksum(s: LZWDecode&) -> u32 {
    var res = s.result_val;
    if s.decoded == s.test_data { res += 100000; }
    return res;
}
