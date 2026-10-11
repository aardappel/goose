// Compress::ArithEncode and Compress::ArithDecode: a 32-bit integer
// arithmetic coder over the byte frequencies of its input, with the
// pending-bit (E3) scaling of the classic Witten-Neal-Cleary coder.

namespace arith;

import std;
import bench;
import compress_data;

// The coded bits and the frequencies the decoder needs to rebuild the model.
struct ArithEncodedResult { frequencies: i32[256], data: u8[>..] }

fn new_arith_encoded_result() -> ArithEncodedResult { ArithEncodedResult { frequencies: [0; 256], data: [] } }

// Cumulative frequencies: symbol s owns [low[s], high[s]) of [0, total).
struct ArithFreqTable { total: i32, low: i32[256], high: i32[256] }

fn new_arith_freq_table(frequencies: i32[:]) -> ArithFreqTable {
    let total = sum(frequencies);
    var low: i32[256] = [0; 256];
    var high: i32[256] = [0; 256];

    var cum: i32 = 0;
    for i in 256 {
        low[i] = cum;
        cum += frequencies[i];
        high[i] = cum;
    }

    return ArithFreqTable { total: total, low: low, high: high };
}

// --- encoding -----------------------------------------------------------------

// Bits are packed most significant first. Goose has no move for a growable
// array, so the stream writes into the array its owner hands it (the
// result's data) instead of giving its own bytes away at flush.
struct BitOutputStream { buffer: u8, bit_pos: i32, bytes: u8[>..]&, bits_written: i32 }

fn new_bit_output_stream(bytes: u8[>..]&) -> BitOutputStream {
    BitOutputStream { buffer: 0, bit_pos: 0, bytes: bytes, bits_written: 0 }
}

fn write_bit(out: BitOutputStream&, bit: i32) {
    out.buffer = (out.buffer << 1) | ((bit as! u8) & 1);
    out.bit_pos += 1;
    out.bits_written += 1;

    if out.bit_pos == 8 {
        out.bytes.push(out.buffer);
        out.buffer = 0;
        out.bit_pos = 0;
    }
}

fn flush(out: BitOutputStream&) {
    if out.bit_pos > 0 {
        out.buffer <<= 8 - out.bit_pos;
        out.bytes.push(out.buffer);
    }
}

fn arith_encode(data: u8[:]) -> ArithEncodedResult {
    var frequencies: i32[256] = [0; 256];
    for byte in data {
        frequencies[byte] += 1;
    }

    let freq_table = new_arith_freq_table(frequencies);

    var low: u64 = 0;
    var high: u64 = 0xFFFFFFFF;
    var pending = 0;
    // The result is built where it is returned to, its bytes in place.
    var encoded = ArithEncodedResult { frequencies: frequencies, data: [] };
    var output = new_bit_output_stream(encoded.data);

    for byte in data {
        let idx = byte;
        let range = high - low + 1;

        high = low + (range * (freq_table.high[idx] as u64) / (freq_table.total as u64)) - 1;
        low = low + (range * (freq_table.low[idx] as u64) / (freq_table.total as u64));

        loop {
            if high < 0x80000000 {
                output.write_bit(0);
                for i in pending {
                    output.write_bit(1);
                }
                pending = 0;
            } else if low >= 0x80000000 {
                output.write_bit(1);
                for i in pending {
                    output.write_bit(0);
                }
                pending = 0;
                low -= 0x80000000;
                high -= 0x80000000;
            } else if low >= 0x40000000 && high < 0xC0000000 {
                pending += 1;
                low -= 0x40000000;
                high -= 0x40000000;
            } else {
                break;
            }

            low <<= 1;
            high = (high << 1) | 1;
            high &= 0xFFFFFFFF;
        }
    }

    pending += 1;
    if low < 0x40000000 {
        output.write_bit(0);
        for i in pending {
            output.write_bit(1);
        }
    } else {
        output.write_bit(1);
        for i in pending {
            output.write_bit(0);
        }
    }

    output.flush();
    return encoded;
}

// The test data is the caller's (see compress_data).
struct ArithEncode { size_val: i64, result_val: u32, test_data: u8[>..<]&, encoded: ArithEncodedResult }

fn new_arith_encode(test_data: u8[>..<]&) -> ArithEncode {
    ArithEncode {
        size_val: config_i64("Compress::ArithEncode", "size"),
        result_val: 0,
        test_data: test_data,
        encoded: new_arith_encoded_result(),
    }
}

fn prepare(s: ArithEncode&) { s.test_data = ::generate_test_data(s.size_val); }

fn run(s: ArithEncode&, iteration_id: i64) {
    s.encoded = arith_encode(s.test_data);
    s.result_val += s.encoded.data.len as! u32;
}

fn checksum(s: ArithEncode&) -> u32 { s.result_val }

// --- decoding -----------------------------------------------------------------

// Reads the bits back most significant first; past the end it reads zeros.
struct BitInputStream { bytes: const u8[:], byte_pos: i64, bit_pos: i32, current_byte: u8 }

fn new_bit_input_stream(bytes: const u8[:]) -> BitInputStream {
    let current_byte: u8 = if bytes.len > 0 { bytes[0] } else { 0 };
    return BitInputStream { bytes: bytes, byte_pos: 0, bit_pos: 0, current_byte: current_byte };
}

fn read_bit(input: BitInputStream&) -> i32 {
    if input.bit_pos == 8 {
        input.byte_pos += 1;
        input.bit_pos = 0;
        input.current_byte = if input.byte_pos < input.bytes.len { input.bytes[input.byte_pos] } else { 0 };
    }

    let bit = (input.current_byte >> (7 - input.bit_pos)) & 1;
    input.bit_pos += 1;
    return bit;
}

fn arith_decode(encoded: ArithEncodedResult&) -> u8[>..] {
    let frequencies: i32[256]& = encoded.frequencies;
    let total = sum(frequencies);
    let data_size = total as i64;

    var low_table: i32[256] = [0; 256];
    var high_table: i32[256] = [0; 256];
    var cum: i32 = 0;
    for i in 256 {
        low_table[i] = cum;
        cum += frequencies[i];
        high_table[i] = cum;
    }

    var result: u8[>..] = [];
    result.resize(data_size, 0);
    var input = new_bit_input_stream(encoded.data);

    var value: u64 = 0;
    for i in 32 {
        value = (value << 1) | input.read_bit() as u64;
    }

    var low: u64 = 0;
    var high: u64 = 0xFFFFFFFF;

    for j in data_size {
        let range = high - low + 1;
        let scaled = ((value - low + 1) * (total as u64) - 1) / range;

        let symbol = lower_bound(high_table) { it as u64 <= scaled } as u8;
        result[j] = symbol;

        high = low + (range * (high_table[symbol] as u64) / (total as u64)) - 1;
        low = low + (range * (low_table[symbol] as u64) / (total as u64));

        loop {
            if high >= 0x80000000 && low < 0x80000000 && (low < 0x40000000 || high >= 0xC0000000) {
                break;
            }

            if high < 0x80000000 {
            } else if low >= 0x80000000 {
                value -= 0x80000000;
                low -= 0x80000000;
                high -= 0x80000000;
            } else if low >= 0x40000000 && high < 0xC0000000 {
                value -= 0x40000000;
                low -= 0x40000000;
                high -= 0x40000000;
            }

            low <<= 1;
            high = (high << 1) | 1;
            value = (value << 1) | input.read_bit() as u64;
        }
    }

    return result;
}

// The test data and the encoded result are the caller's (see compress_data).
struct ArithDecode {
    size_val: i64,
    result_val: u32,
    test_data: u8[>..<]&,
    encoded: ArithEncodedResult&,
    decoded: u8[>..],
}

fn new_arith_decode(test_data: u8[>..<]&, encoded: ArithEncodedResult&) -> ArithDecode {
    ArithDecode {
        size_val: config_i64("Compress::ArithDecode", "size"),
        result_val: 0,
        test_data: test_data,
        encoded: encoded,
        decoded: [],
    }
}

fn prepare(s: ArithDecode&) {
    s.test_data = ::generate_test_data(s.size_val);

    var encoder_test_data: u8[>..<] = [];
    var encoder = new_arith_encode(encoder_test_data);
    encoder.size_val = s.size_val;
    encoder.prepare();
    encoder.test_data = copy(s.test_data);
    encoder.run(0);
    // Goose has no move for a growable array: the encoder's result is copied.
    s.encoded = copy(encoder.encoded);
}

fn run(s: ArithDecode&, iteration_id: i64) {
    s.decoded = arith_decode(s.encoded);
    s.result_val += s.decoded.len as! u32;
}

fn checksum(s: ArithDecode&) -> u32 {
    var res = s.result_val;
    if s.decoded == s.test_data { res += 100000; }
    return res;
}
