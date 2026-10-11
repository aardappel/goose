// Compress::HuffEncode and Compress::HuffDecode: Huffman coding of a repeated
// "ABRACADABRA". The code tree is rebuilt from the byte frequencies on every
// run, and decoding walks it one bit at a time.

namespace huffman;

import std;
import bench;
import compress_data;

// A leaf stands for a byte; an inner node has both children. The nodes of a
// tree live in a pool its builder is given, linked by references.
struct HuffmanNode {
    frequency: i32,
    byte_val: u8,
    is_leaf: bool,
    left: HuffmanNode?,
    right: HuffmanNode?,
}

fn new_huffman_node(freq: i32, byte: u8, leaf: bool) -> HuffmanNode {
    HuffmanNode { frequency: freq, byte_val: byte, is_leaf: leaf, left: null, right: null }
}

// Each byte's code: the code_lengths[byte] low bits of codes[byte], most
// significant first.
struct HuffmanCodes { code_lengths: i32[256], codes: i32[256] }

fn new_huffman_codes() -> HuffmanCodes {
    HuffmanCodes { code_lengths: [0; 256], codes: [0; 256] }
}

struct EncodedResult { bit_count: i32, frequencies: i32[256], data: u8[>..] }

fn new_encoded_result() -> EncodedResult { EncodedResult { bit_count: 0, frequencies: [0; 256], data: [] } }

// The tree for `frequencies`, its nodes pushed into `pool`; null when no byte
// occurs.
fn build_huffman_tree(frequencies: i32[:], pool: HuffmanNode[>..]&) -> HuffmanNode? {
    var nodes: (HuffmanNode&)[>..<] = [];
    for freq, i in frequencies {
        if freq > 0 { nodes.push(pool.push(new_huffman_node(freq, i as! u8, true))); }
    }

    stable_sort(nodes) { a, b => a.frequency < b.frequency };

    if nodes.len == 1 {
        let node .= nodes[0];
        let root .= pool.push(new_huffman_node(node.frequency, 0, false));
        root.left .= node;
        root.right .= pool.push(new_huffman_node(0, 0, true));
        return root;
    }

    while nodes.len > 1 {
        let left .= nodes[0];
        remove_at(nodes, 0);
        let right .= nodes[0];
        remove_at(nodes, 0);

        let parent .= pool.push(new_huffman_node(left.frequency + right.frequency, 0, false));
        parent.left .= left;
        parent.right .= right;

        let pos = lower_bound(nodes) { it.frequency < parent.frequency };
        insert_at(nodes, pos, &parent);
    }

    if nodes.len == 0 { return null; }
    return nodes.pop();
}

recursive fn build_huffman_codes(node: HuffmanNode&, code: i32, length: i32, huffman_codes: HuffmanCodes&) {
    if node.is_leaf {
        if length > 0 || node.byte_val != 0 {
            let idx = node.byte_val;
            huffman_codes.code_lengths[idx] = length;
            huffman_codes.codes[idx] = code;
        }
    } else {
        let left = node.left;
        if left { build_huffman_codes(left, code << 1, length + 1, huffman_codes); }
        let right = node.right;
        if right { build_huffman_codes(right, (code << 1) | 1, length + 1, huffman_codes); }
    }
}

fn huffman_encode(data: u8[:], codes: HuffmanCodes&, frequencies: i32[256]&) -> EncodedResult {
    // The result is built where it is returned to, its bytes in place.
    var encoded = EncodedResult { bit_count: 0, frequencies: frequencies, data: [] };
    let result: u8[>..]& = encoded.data;
    var current_byte: u8 = 0;
    var bit_pos = 0;
    var total_bits: i32 = 0;

    for byte in data {
        let idx = byte;
        let code = codes.codes[idx];
        let length = codes.code_lengths[idx];

        // i from length - 1 down to 0.
        for j in length {
            let i = length - 1 - j;
            if code & (1 << i) != 0 {
                current_byte |= 1 << (7 - bit_pos);
            }
            bit_pos += 1;
            total_bits += 1;

            if bit_pos == 8 {
                result.push(current_byte);
                current_byte = 0;
                bit_pos = 0;
            }
        }
    }

    if bit_pos > 0 {
        result.push(current_byte);
    }

    encoded.bit_count = total_bits;
    return encoded;
}

// The test data is the caller's (see compress_data).
struct HuffEncode { size_val: i64, result_val: u32, test_data: u8[>..<]&, encoded: EncodedResult }

fn new_huff_encode(test_data: u8[>..<]&) -> HuffEncode {
    HuffEncode {
        size_val: config_i64("Compress::HuffEncode", "size"),
        result_val: 0,
        test_data: test_data,
        encoded: new_encoded_result(),
    }
}

fn prepare(s: HuffEncode&) { s.test_data = ::generate_test_data(s.size_val); }

fn run(s: HuffEncode&, iteration_id: i64) {
    var frequencies: i32[256] = [0; 256];
    for byte in s.test_data {
        frequencies[byte] += 1;
    }

    var pool: HuffmanNode[>..] = [];
    let tree = build_huffman_tree(frequencies, pool);

    var codes = new_huffman_codes();
    if tree {
        build_huffman_codes(tree, 0, 0, codes);
    }

    s.encoded = huffman_encode(s.test_data, codes, frequencies);
    s.result_val += s.encoded.data.len as! u32;
}

fn checksum(s: HuffEncode&) -> u32 { s.result_val }

fn huffman_decode(encoded: u8[:], root: HuffmanNode&, bit_count: i32) -> u8[>..] {
    var result: u8[>..] = [];
    result.resize(bit_count, 0);
    var result_size = 0;

    var current_node .= root;
    var bits_processed: i32 = 0;
    var byte_index = 0;

    while bits_processed < bit_count && byte_index < encoded.len {
        let byte_val = encoded[byte_index];
        byte_index += 1;

        // bit_pos from 7 down to 0.
        for j in 8 {
            let bit_pos = 7 - j;
            if bits_processed >= bit_count {
                break;
            }

            let bit = (byte_val >> bit_pos) & 1 == 1;
            bits_processed += 1;

            let child = if bit { current_node.right } else { current_node.left };
            assert(child);
            current_node .= child;

            if current_node.is_leaf {
                if current_node.byte_val != 0 {
                    result[result_size] = current_node.byte_val;
                    result_size += 1;
                }
                current_node .= root;
            }
        }
    }

    result.resize(result_size);
    return result;
}

// The test data and the encoded result are the caller's (see compress_data).
struct HuffDecode {
    size_val: i64,
    result_val: u32,
    test_data: u8[>..<]&,
    encoded: EncodedResult&,
    decoded: u8[>..],
}

fn new_huff_decode(test_data: u8[>..<]&, encoded: EncodedResult&) -> HuffDecode {
    HuffDecode {
        size_val: config_i64("Compress::HuffDecode", "size"),
        result_val: 0,
        test_data: test_data,
        encoded: encoded,
        decoded: [],
    }
}

fn prepare(s: HuffDecode&) {
    s.test_data = ::generate_test_data(s.size_val);

    var encoder_test_data: u8[>..<] = [];
    var encoder = new_huff_encode(encoder_test_data);
    encoder.size_val = s.size_val;
    encoder.prepare();
    encoder.run(0);
    // Goose has no move for a growable array: the encoder's result is copied.
    s.encoded = copy(encoder.encoded);
}

fn run(s: HuffDecode&, iteration_id: i64) {
    var pool: HuffmanNode[>..] = [];
    let tree = build_huffman_tree(s.encoded.frequencies, pool);
    if tree {
        s.decoded = huffman_decode(s.encoded.data, tree, s.encoded.bit_count);
        s.result_val += s.decoded.len as! u32;
    }
}

fn checksum(s: HuffDecode&) -> u32 {
    var res = s.result_val;
    if s.decoded == s.test_data { res += 100000; }
    return res;
}
