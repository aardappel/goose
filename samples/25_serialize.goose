// Data that is already its own file format. A structure whose links are
// self-relative offsets rather than addresses means the same wherever it
// sits, so saving it is a write of its bytes and loading it is a read --
// no serializer, no parser, no pointer fixups, no allocation per node.
//
// The catch is that a file is not trustworthy the way a value the compiler
// built is. `from_bytes` therefore *verifies* before the bytes become a
// value: it walks the image, checks that it frames exactly, and checks
// every link lands on the start of an element of that same image. It
// returns the array and a bool, and a rejected image gives an empty array
// -- so a corrupt or hostile file is a `false`, never a wild reference.
//
// Here that is a word index over a book: build it, save it, load it back,
// and answer the same queries against the loaded copy. Then a compact
// variable-size form of the same data, and then a tampered file.
//
// Build and run (from this directory):
//   goose -o serialize.c 25_serialize.goose && cl serialize.c && serialize

import std;
import dictionary;
import os;

// --- the index ---------------------------------------------------------------
// A binary search tree in one grow-only array, exactly as 14_bst builds one,
// except the key is inline: a limited array, so the node stays fixed-size and
// the pool stays indexable. Nothing in a node is an address -- `left` and
// `right` are 4-byte offsets from their own field to the child.

struct Node { word: u8[..24], count: i32, left: Node&<u32>?, right: Node&<u32>? }

fn insert(pool: Node[>..]&, word: u8[:], count: i32) {
    if pool.len == 0 { pool.push(Node { word: word, count: count }); return; }
    var cur .= pool[0];
    loop {
        let c = compare(word, cur.word[..]);
        if c == 0 { cur.count += count; return; }
        if c < 0 {
            let l = cur.left;
            if l { cur .= l; continue; }
            cur.left .= pool.push(Node { word: word, count: count });
            return;
        }
        let r = cur.right;
        if r { cur .= r; continue; }
        cur.right .= pool.push(Node { word: word, count: count });
        return;
    }
}

fn lookup(pool: Node[>..]&, word: u8[:]) -> i32 {
    guard pool.len > 0 else { return 0; }
    var cur .= pool[0];
    loop {
        let c = compare(word, cur.word[..]);
        if c == 0 { return cur.count; }
        let next = if c < 0 { cur.left } else { cur.right };
        guard next else { return 0; }
        cur .= next;
    }
}

// The deepest path, which is what says the loaded tree really is the tree
// and not merely the same bytes.
recursive fn depth(n: Node&) -> i64 {
    let l = n.left;
    let r = n.right;
    var d = 0;
    if l { d = depth(l); }
    if r { d = max(d, depth(r)); }
    return d + 1;
}

// --- the compact form --------------------------------------------------------
// The same tree with variable-size elements: the word inline behind a varint
// length, the links varint offsets. A node costs what its word costs plus a
// byte or two per link, and the pool is walked rather than indexed. The
// verifier handles both -- for fixed elements a link is valid when it is a
// multiple of the element size, and for variable ones the framing pass
// records where each element began and the link pass asks whether an offset
// is one of those.

enum Tiny {
    Leaf { word: u8[varint], count: varint },
    Fork { word: u8[varint], count: varint,
           left: Tiny..&<varint>?, right: Tiny..&<varint>? },
}

// Rebuilt bottom-up from the big tree: children are pushed before their
// parent, so a parent's links are short backward offsets.
recursive fn compact(n: Node&, out: Tiny..[>..]&) -> Tiny..& {
    let l = n.left;
    let r = n.right;
    guard l || r else {
        return out.push(Tiny.Leaf { word: n.word[..], count: n.count });
    }
    // Both subtrees land in `out` first; `push` hands back a reference to
    // each, and both are rooted at `out`, which is what lets the parent
    // store them as offsets (§3.9).
    if l {
        let lc: Tiny..& = compact(l, out);
        if r {
            let rc: Tiny..& = compact(r, out);
            return out.push(Tiny.Fork { word: n.word[..], count: n.count,
                                        left: lc, right: rc });
        }
        return out.push(Tiny.Fork { word: n.word[..], count: n.count, left: lc });
    }
    guard r else { return out.push(Tiny.Leaf { word: n.word[..], count: n.count }); }
    let rc: Tiny..& = compact(r, out);
    return out.push(Tiny.Fork { word: n.word[..], count: n.count, right: rc });
}

fn total(pool: Tiny..[>..]&) -> i64 {
    var sum = 0;
    for t in pool {
        match t {
            Leaf &l => { sum += l.count; },
            Fork &f => { sum += f.count; }
        }
    }
    return sum;
}

// --- main --------------------------------------------------------------------

fn is_letter(c: u8) -> bool { (c >= 'a' && c <= 'z') || c == '\'' }

fn main() {
    var path: u8[>..] = [];
    if arg_count() > 1 { arg(1, path); } else { path.append("data/alice.txt"); }
    var text: u8[>..] = [];
    guard read_file(path, text) else { print("cannot read ", path); exit(1); }
    to_lower(text);

    // Count the words, then index them. Keys are slices into `text`, so
    // nothing is copied until a word reaches a node.
    var counts = dictionary<u8[:], i32> {};
    var i = 0;
    while i < text.len {
        while i < text.len && !is_letter(text[i]) { i++; }
        let start = i;
        while i < text.len && is_letter(text[i]) { i++; }
        guard i > start && i - start <= 24 else { continue; }
        counts.update(text[start..i], 0) { it += 1; };
    }

    var index: Node[>..] = [];
    counts.each() { k, v => insert(index, k, v); };
    print("indexed ", index.len, " words from ", path);

    // Saving is a write of the element region. There is no serializer to
    // write and no schema to keep in step: the bytes the program has been
    // using all along are the file. `bytes_of` is a *view* of them -- no
    // copy at all -- and the framing prefix `from_bytes` wants is a ULEB
    // byte count, so the save is a tiny header and then the payload.
    let payload = index.bytes_of();
    var head: u8[>..] = [];
    format_uleb(head, payload.len);
    guard write_file("index.gsx", head) && append_file("index.gsx", payload)
    else { print("cannot write"); exit(1); }
    print("image ", head.len + payload.len, " bytes = ", index.len, " nodes x ",
          payload.len / index.len, " + ", head.len);

    // The same image built as one value instead, for the checks below.
    var image: u8[>..] = index.to_bytes();

    // Loading is a read, a verify and a copy. What comes back is an ordinary
    // array rooted at `loaded`: it indexes, it grows, references into it are
    // ordinary references, and §9 needs no new rule for it.
    var file: u8[>..] = [];
    guard read_file("index.gsx", file) else { print("cannot re-read"); exit(1); }
    let n, k = parse_uleb(file[0..]);
    print("prefix says ", n, " bytes after ", k, ", file has ", file.len - k);
    let t0 = clock();
    var loaded, ok = from_bytes<Node[>..]>(file);
    let t1 = clock();
    print("loaded ", ok, ", ", loaded.len, " nodes, same size ",
          loaded.len == index.len);

    // The same queries against the loaded copy.
    var same = true;
    each_split("alice rabbit down think curiosity nonexistentword", ' ') {
        let a = lookup(index, it);
        let b = lookup(loaded, it);
        if a != b { same = false; }
        print("  ", it, " = ", b);
    };
    print("answers match ", same, ", depth ", depth(loaded[0]),
          " = ", depth(index[0]));

    // The compact form: variable-size elements, varint links.
    var tiny: Tiny..[>..] = [];
    compact(index[0], tiny);
    var timage: u8[>..] = tiny.to_bytes();
    var tback, tok = from_bytes<Tiny..[>..]>(timage);
    print("compact ", timage.len, " bytes for ", tiny.len, " nodes, reload ",
          tok, " totals ", total(tback) == total(tiny));

    // What a verifier is for. Flipping a byte of a link makes an offset that
    // points somewhere the array does not reach; in a language that trusted
    // the file, following it would be a wild read. Here it is a `false`.
    var tampered: u8[>..] = [];
    tampered.append(image);
    let pfx = image.len - payload.len;          // the framing prefix
    tampered[pfx + 29] = tampered[pfx + 29] ^ 0x40;   // inside the first link
    var bad, bok = from_bytes<Node[>..]>(tampered);
    print("tampered link ", bok, ", nodes ", bad.len);

    // Verification is about safety, not integrity: a byte inside a *count*
    // still describes a valid tree, so the image is accepted with the wrong
    // number in it. A checksum is the answer to that, and it is not what
    // `from_bytes` promises.
    var edited: u8[>..] = [];
    edited.append(image);
    edited[pfx + 25] = edited[pfx + 25] ^ 0x01;      // inside the first count
    var ed, eok = from_bytes<Node[>..]>(edited);
    print("edited count ", eok, ", still a tree ", ed.len == index.len);

    // Truncation, extension, and an empty file.
    var cut: u8[>..] = [];
    cut.append(image);
    cut.pop();
    var c1, cok = from_bytes<Node[>..]>(cut);
    var grown: u8[>..] = [];
    grown.append(image);
    grown.push(0);
    var g1, gok = from_bytes<Node[>..]>(grown);
    var empty: Node[>..] = [];
    var e1, eok2 = from_bytes<Node[>..]>(empty.to_bytes());
    var z1, zok = from_bytes<Node[>..]>("");
    print("truncated ", cok, ", extended ", gok, ", empty ", eok2, " ", e1.len,
          ", no prefix ", zok);

    // The image is an ordinary resizable result, so it also goes straight
    // into a message with a header of its own -- one copy, not two.
    var msg: u8[>..] = [];
    msg.append("GSX1");
    index.to_bytes(msg);
    var wire, wok = from_bytes<Node[>..]>(msg[4..]);
    print("message ", msg.len, " bytes, tag ", msg[0..4], ", reload ", wok, " ",
          wire.len == index.len);

    delete_file("index.gsx");
    format_stderr("verify+copy of ", image.len, " bytes: ", (t1 - t0) * 1000.0,
                  " ms\n");
}

fn format_stderr(a: u8[:], b: i64, c: u8[:], d: f64, e: u8[:]) {
    var line: u8[>..] = [];
    format(line, a, b, c, d, e);
    write_stderr(line);
}
