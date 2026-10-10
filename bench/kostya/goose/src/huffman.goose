// Compress::HuffEncode and Compress::HuffDecode: Huffman coding of a repeated
// "ABRACADABRA". The code tree is rebuilt from the byte frequencies on every
// run, as a pool of nodes linked by references, and decoding walks it one
// bit at a time.

namespace huffman;

import std;
import bench;
import compress_data;

// A leaf stands for a byte; an inner node has both children. Structs are
// packed, so the links come first and the node is padded to 24 bytes to keep
// them 8-byte aligned in a pool.
struct Node {
    left: Node?,
    right: Node?,
    frequency: i32,
    byte_val: u8,
    is_leaf: bool,
    pad 2,
}

fn leaf(frequency: i32, byte_val: u8) -> Node {
    Node { frequency: frequency, byte_val: byte_val, is_leaf: true, .. }
}

// A byte's code: its `length` low bits, most significant first.
struct Code { bits: i32, length: i32 }

// Builds the tree for `frequencies` into `tree` and returns its root, or null
// when no byte occurs. The work list holds the subtrees still to be merged,
// as indices into `tree`, ordered by frequency.
fn build_huffman_tree(frequencies: i32[256]&, tree: Node[>..]&) -> Node? {
    var queue: i32[>..<] = [];
    for f, c in frequencies {
        if f > 0 {
            queue.push(tree.len as i32);
            tree.push(leaf(f, c as! u8));
        }
    }
    stable_sort(queue) { a, b => tree[a].frequency < tree[b].frequency };

    if queue.len == 0 { return null; }
    if queue.len == 1 {
        let only .= tree[queue[0]];
        let empty .= tree.push(leaf(0, 0));
        return tree.push(Node { left: only, right: empty, frequency: only.frequency,
                                byte_val: 0, is_leaf: false });
    }

    while queue.len > 1 {
        let left = remove_at(queue, 0);
        let right = remove_at(queue, 0);
        let frequency = tree[left].frequency + tree[right].frequency;
        let parent = tree.len as i32;
        tree.push(Node { left: tree[left], right: tree[right], frequency: frequency,
                         byte_val: 0, is_leaf: false });
        let pos = lower_bound(queue) { tree[it].frequency < frequency };
        insert_at(queue, pos, parent);
    }
    return tree[queue[0]];
}

// Records the path to every leaf below `node` as that leaf's code.
recursive fn build_huffman_codes(node: Node&, bits: i32, length: i32, codes: Code[256]&) {
    if node.is_leaf {
        if length > 0 || node.byte_val != 0 {
            codes[node.byte_val] = Code { bits: bits, length: length };
        }
        return;
    }
    let left = node.left;
    if left { build_huffman_codes(left, bits << 1, length + 1, codes); }
    let right = node.right;
    if right { build_huffman_codes(right, (bits << 1) | 1, length + 1, codes); }
}

// `data` packed into bytes as the concatenation of its bytes' codes, and the
// number of bits that holds.
fn huffman_encode(data: u8[:], codes: Code[256]&) -> u8[>..], i64 {
    var result: u8[>..] = [];
    var current_byte: u8 = 0;
    var bit_pos = 0;
    var total_bits = 0;
    for byte in data {
        let code = codes[byte];
        for j in 0..code.length {
            let i = code.length - 1 - j;
            if code.bits & (1 << i) != 0 { current_byte |= 1 << (7 - bit_pos); }
            bit_pos++;
            total_bits++;
            if bit_pos == 8 {
                result.push(current_byte);
                current_byte = 0;
                bit_pos = 0;
            }
        }
    }
    if bit_pos > 0 { result.push(current_byte); }
    return result, total_bits;
}

fn byte_frequencies(data: u8[:]) -> i32[256] {
    var frequencies: i32[256] = [0; 256];
    for c in data { frequencies[c] += 1; }
    return frequencies;
}

// Encodes `data` with the codes of the tree built for its `frequencies`.
fn compress(data: u8[:], frequencies: i32[256]&) -> u8[>..], i64 {
    var tree: Node[>..] = [];
    var codes: Code[256] = [Code { 0, 0 }; 256];
    let root = build_huffman_tree(frequencies, tree);
    if root { build_huffman_codes(root, 0, 0, codes); }
    return huffman_encode(data, codes);
}

fn run_encode(b: Bench&) -> u32 {
    let data = ::generate_test_data(config_i64(b.name, "size"));
    var checksum: u32 = 0;
    measure(b) {
        let frequencies = byte_frequencies(data);
        let encoded, bit_count = compress(data, frequencies);
        checksum += encoded.len as! u32;
    };
    return checksum;
}

// Walks the tree from the root, one bit of `encoded` per step, emitting a
// leaf's byte and starting over at the root whenever it reaches one.
fn huffman_decode(encoded: u8[:], root: Node&, bit_count: i64) -> u8[>..] {
    var result: u8[>..] = [];
    var current .= root;
    var bits_processed = 0;
    var byte_index = 0;
    while bits_processed < bit_count && byte_index < encoded.len {
        let byte_val = encoded[byte_index];
        byte_index++;
        var bit_pos = 8;
        while bit_pos > 0 {
            bit_pos--;
            if bits_processed >= bit_count { break; }
            let bit = (byte_val >> bit_pos) & 1 == 1;
            bits_processed++;
            let child = if bit { current.right } else { current.left };
            assert(child);
            current .= child;
            if current.is_leaf {
                if current.byte_val != 0 { result.push(current.byte_val); }
                current .= root;
            }
        }
    }
    return result;
}

fn run_decode(b: Bench&) -> u32 {
    let data = ::generate_test_data(config_i64(b.name, "size"));
    let frequencies = byte_frequencies(data);
    let encoded, bit_count = compress(data, frequencies);
    var decoded: u8[>..] = [];
    var checksum: u32 = 0;
    measure(b) {
        var tree: Node[>..] = [];
        let root = build_huffman_tree(frequencies, tree);
        if root {
            decoded = huffman_decode(encoded, root, bit_count);
            checksum += decoded.len as! u32;
        }
    };
    if decoded == data { checksum += 100000; }
    return checksum;
}
