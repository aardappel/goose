// Huffman coding, end to end: count byte frequencies, build the code tree
// with a priority queue, read the codes off the tree, pack the text into
// bits, and decode it again. The tree lives in one array of nodes whose
// links are 2-byte self-relative offsets -- a node is 12 bytes and the tree
// for a 256-symbol alphabet fits in 6 KB -- and the priority queue is a
// grow-shrink array of node indices heap-ordered by weight.
//
// Build and run (from this directory):
//   goose -o huffman.c huffman.goose && cl huffman.c && huffman

import std;

// A leaf has a symbol; an inner node has two children. The links point
// within the `nodes` array the node is stored in, never anywhere else.
struct Node { weight: i32, sym: i16, left: Node&<u16>?, right: Node&<u16>? }
struct Code { bits: u32, len: u8 }

let TEXT = "it was the best of times, it was the worst of times, it was the age of wisdom, it was the age of foolishness, it was the epoch of belief, it was the epoch of incredulity, it was the season of light, it was the season of darkness, it was the spring of hope, it was the winter of despair, we had everything before us, we had nothing before us, we were all going direct to heaven, we were all going direct the other way";

// Walks the tree, recording the path to each leaf as its code. Recursion
// passes the table down by reference; the tree nodes are referenced through
// their relative links, which load as ordinary references.
recursive fn assign(n: Node&, bits: u32, len: i64, codes: Code[256]&) {
    if n.sym >= 0 {
        codes[n.sym] = Code { bits: bits, len: len as! u8 };
        return;
    }
    let l = n.left;
    let r = n.right;
    assert(l);
    assert(r);
    assign(l, bits << 1, len + 1, codes);
    assign(r, bits << 1 | 1, len + 1, codes);
}

fn main() {
    // 1. Frequencies.
    var freq: i32[256] = [0; 256];
    for c in TEXT { freq[c] += 1; }

    // 2. One leaf per used symbol, all in the priority queue.
    var nodes: Node[>..] = [];
    var heap: i32[>..<] = [];
    for f, c in freq {
        if f > 0 {
            nodes.push(Node { weight: f, sym: c as i16 });
            heap.heap_push((nodes.len - 1) as i32) { a, b => nodes[a].weight < nodes[b].weight };
        }
    }
    print(heap.len, " distinct symbols in ", TEXT.len, " bytes");

    // 3. Merge the two lightest until one tree remains. The new node links
    //    to its children by offset within the same array it is pushed into.
    while heap.len > 1 {
        let a = heap.heap_pop() { a, b => nodes[a].weight < nodes[b].weight } as i64;
        let b = heap.heap_pop() { a, b => nodes[a].weight < nodes[b].weight } as i64;
        nodes.push(Node { weight: nodes[a].weight + nodes[b].weight, sym: -1,
                          left: nodes[a], right: nodes[b] });
        heap.heap_push((nodes.len - 1) as i32) { a, b => nodes[a].weight < nodes[b].weight };
    }
    let root .= nodes[heap.pop()];
    print(nodes.len, " nodes, ", nodes.len * 12, " bytes of tree");

    // 4. Codes.
    var codes: Code[256] = [default<Code>(); 256];
    assign(root, 0, 0, codes);
    var line: u8[>..] = [];
    for c in "aeiost ," {
        line.clear();
        line.push('\'');
        if c == ' ' { line.append("space"); } else { line.push(c); }
        format(line, "' x", freq[c], ": ");
        format_uint(line, codes[c].bits as u64, 2, codes[c].len, '0');
        print(line);
    }

    // 5. Encode: append each code's bits, most significant first, into a
    //    byte array; `nbits` is the real length.
    var packed: u8[>..] = [];
    var nbits = 0;
    for c in TEXT {
        let code = codes[c];
        var k = code.len as i64;
        while k > 0 {
            k--;
            if nbits % 8 == 0 { packed.push(0); }
            if (code.bits >> k) & 1 == 1 { packed[nbits / 8] |= (0x80 >> (nbits % 8)) as! u8; }
            nbits++;
        }
    }
    print(nbits, " bits = ", packed.len, " bytes; ", (packed.len * 100) / TEXT.len, "% of the original");

    // 6. Decode: walk from the root, one bit per step, emitting at leaves.
    var out: u8[>..] = [];
    var cur .= root;
    for i in nbits {
        let bit = (packed[i / 8] >> (7 - i % 8)) & 1;
        let next = if bit == 0 { cur.left } else { cur.right };
        assert(next);
        cur .= next;
        if cur.sym >= 0 {
            out.push(cur.sym as! u8);
            cur .= root;
        }
    }
    print("round trip ok: ", out == TEXT);
    print(out[..40]);
}
