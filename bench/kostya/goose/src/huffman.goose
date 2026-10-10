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

fn run_encode(b: Bench&) -> u32 {
    let test_data = ::generate_test_data(config_i64(b.name, "size"));
    var encoded = EncodedResult { bit_count: 0, frequencies: [0; 256], data: [] };
    var result_val: u32 = 0;
    measure(b) {
        var frequencies: i32[256] = [0; 256];
        for byte in test_data {
            frequencies[byte] += 1;
        }

        var pool: HuffmanNode[>..] = [];
        let tree = build_huffman_tree(frequencies, pool);

        var codes = new_huffman_codes();
        if tree {
            build_huffman_codes(tree, 0, 0, codes);
        }

        encoded = huffman_encode(test_data, codes, frequencies);
        result_val += encoded.data.len as! u32;
    };
    return result_val;
}

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

fn run_decode(b: Bench&) -> u32 {
    let test_data = ::generate_test_data(config_i64(b.name, "size"));

    // HuffEncode's prepare and run(0).
    var frequencies: i32[256] = [0; 256];
    for byte in test_data {
        frequencies[byte] += 1;
    }
    var encoder_pool: HuffmanNode[>..] = [];
    let encoder_tree = build_huffman_tree(frequencies, encoder_pool);
    var codes = new_huffman_codes();
    if encoder_tree {
        build_huffman_codes(encoder_tree, 0, 0, codes);
    }
    let encoded = huffman_encode(test_data, codes, frequencies);

    var decoded: u8[>..] = [];
    var result_val: u32 = 0;
    measure(b) {
        var pool: HuffmanNode[>..] = [];
        let tree = build_huffman_tree(encoded.frequencies, pool);
        if tree {
            decoded = huffman_decode(encoded.data, tree, encoded.bit_count);
            result_val += decoded.len as! u32;
        }
    };
    if decoded == test_data { result_val += 100000; }
    return result_val;
}
