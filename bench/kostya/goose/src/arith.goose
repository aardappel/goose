// Compress::ArithEncode and Compress::ArithDecode: a 32-bit integer
// arithmetic coder over the byte frequencies of its input, with the
// pending-bit (E3) scaling of the classic Witten-Neal-Cleary coder.

namespace arith;

import std;
import bench;
import compress_data;

// The coded bits and the frequencies the decoder needs to rebuild the model.
struct Encoded { frequencies: u32[256], data: u8[>..] }

// Cumulative frequencies: symbol s owns [low[s], high[s]) of [0, total).
struct FreqTable { total: u32, low: u32[256], high: u32[256] }

fn freq_table(frequencies: u32[256]&) -> FreqTable {
    var t = FreqTable { total: 0, low: [0; 256], high: [0; 256] };
    var cum: u32 = 0;
    for i in 256 {
        t.low[i] = cum;
        cum += frequencies[i];
        t.high[i] = cum;
    }
    t.total = cum;
    return t;
}

let HALF: u64 = 0x80000000;
let QUARTER: u64 = 0x40000000;
let THREE_QUARTERS: u64 = 0xC0000000;

// --- encoding -----------------------------------------------------------------

// Bits are packed most significant first into `bytes`.
struct BitOutputStream { buffer: u8, bit_pos: i32, bytes: u8[>..]& }

fn write_bit(out: BitOutputStream&, bit: u8) {
    out.buffer = (out.buffer << 1) | (bit & 1);
    out.bit_pos += 1;
    if out.bit_pos == 8 {
        out.bytes.push(out.buffer);
        out.buffer = 0;
        out.bit_pos = 0;
    }
}

// A bit followed by the pending opposite bits the E3 scaling deferred.
fn write_bits(out: BitOutputStream&, bit: u8, pending: i64) {
    out.write_bit(bit);
    for i in pending { out.write_bit(1 - bit); }
}

fn flush(out: BitOutputStream&) {
    if out.bit_pos > 0 {
        out.buffer <<= 8 - out.bit_pos;
        out.bytes.push(out.buffer);
    }
}

fn encode(data: u8[:]) -> Encoded {
    var result = Encoded { frequencies: [0; 256], data: [] };
    for byte in data { result.frequencies[byte] += 1; }
    let table = freq_table(result.frequencies);
    let total: u64 = table.total;

    var low: u64 = 0;
    var high: u64 = 0xFFFFFFFF;
    var pending = 0;
    var output = BitOutputStream { buffer: 0, bit_pos: 0, bytes: result.data };

    for byte in data {
        let range = high - low + 1;
        high = low + range * table.high[byte] / total - 1;
        low = low + range * table.low[byte] / total;

        loop {
            if high < HALF {
                output.write_bits(0, pending);
                pending = 0;
            } else if low >= HALF {
                output.write_bits(1, pending);
                pending = 0;
                low -= HALF;
                high -= HALF;
            } else if low >= QUARTER && high < THREE_QUARTERS {
                pending += 1;
                low -= QUARTER;
                high -= QUARTER;
            } else {
                break;
            }
            low <<= 1;
            high = ((high << 1) | 1) & 0xFFFFFFFF;
        }
    }

    pending += 1;
    output.write_bits(if low < QUARTER { 0 } else { 1 }, pending);
    output.flush();
    return result;
}

fn run_encode(b: Bench&) -> u32 {
    let test_data = ::generate_test_data(config_i64(b.name, "size"));
    var result: u32 = 0;
    measure(b) {
        let encoded = encode(test_data);
        result += encoded.data.len as! u32;
    };
    return result;
}

// --- decoding -----------------------------------------------------------------

// Reads the bits back most significant first; past the end it reads zeros.
struct BitInputStream { bytes: const u8[:], byte_pos: i64, bit_pos: i32, current_byte: u8 }

fn bit_input(bytes: const u8[:]) -> BitInputStream {
    let first: u8 = if bytes.len > 0 { bytes[0] } else { 0 };
    return BitInputStream { bytes: bytes, byte_pos: 0, bit_pos: 0, current_byte: first };
}

fn read_bit(input: BitInputStream&) -> u64 {
    if input.bit_pos == 8 {
        input.byte_pos += 1;
        input.bit_pos = 0;
        input.current_byte = if input.byte_pos < input.bytes.len { input.bytes[input.byte_pos] } else { 0 };
    }
    let bit = (input.current_byte >> (7 - input.bit_pos)) & 1;
    input.bit_pos += 1;
    return bit;
}

fn decode(encoded: Encoded&) -> u8[>..] {
    let table = freq_table(encoded.frequencies);
    let total: u64 = table.total;

    var result: u8[>..] = [];
    var input = bit_input(encoded.data);
    var value: u64 = 0;
    for i in 32 { value = (value << 1) | input.read_bit(); }

    var low: u64 = 0;
    var high: u64 = 0xFFFFFFFF;

    for j in table.total {
        let range = high - low + 1;
        let scaled = ((value - low + 1) * total - 1) / range;
        let symbol = lower_bound(table.high) { it <= scaled } as! u8;
        result.push(symbol);

        high = low + range * table.high[symbol] / total - 1;
        low = low + range * table.low[symbol] / total;

        loop {
            if high < HALF {
            } else if low >= HALF {
                value -= HALF;
                low -= HALF;
                high -= HALF;
            } else if low >= QUARTER && high < THREE_QUARTERS {
                value -= QUARTER;
                low -= QUARTER;
                high -= QUARTER;
            } else {
                break;
            }
            low <<= 1;
            high = (high << 1) | 1;
            value = (value << 1) | input.read_bit();
        }
    }
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
