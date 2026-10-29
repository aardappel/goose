// stdlib/std.goose's byte sets, byte by byte: every byte value at every
// position of searches 16 to 79 bytes long, over a filler that is not in the
// set, compared with plain loops. Where stdlib_byteset.goose searches
// random text, this reaches each byte's path through the block tests (every
// lane of a block, each block of a wider step, the overlapping last block,
// the tables' lookups on either side of 0x80), for sets of every kind and
// their complements, from two starts.
import std;

fn plain_any(s: u8[:], m: bool[256]&) -> i64 {
    for c, i in s { if m[c] { return i; } }
    return -1;
}

fn plain_pair(s: u8[:], a: bool[256]&, d: i64, b: bool[256]&) -> i64 {
    var i = 0;
    while i + d < s.len {
        if a[s[i]] && b[s[i + d]] { return i; }
        i += 1;
    }
    return -1;
}

fn of(bytes: u8[:]) -> bool[256] {
    var m: bool[256] = [false; 256];
    for b in bytes { m[b] = true; }
    return m;
}

fn not(m: bool[256]) -> bool[256] {
    var r = m;
    for &x in r { x = !x; }
    return r;
}

fn range(lo: i64, hi: i64) -> bool[256] {
    var m: bool[256] = [false; 256];
    for b in lo..hi + 1 { m[b] = true; }
    return m;
}

// The first byte not in m, or -1 for the set of every byte.
fn outside(m: bool[256]&) -> i64 {
    for x, b in m { if !x { return b; } }
    return -1;
}

fn main() {
    var sets: (bool[256])[>..] = [];
    sets.push(of("a"));
    sets.push(of([0]));
    sets.push(of([0x80]));
    sets.push(of([0xff]));
    sets.push(of("ab"));
    sets.push(of([0x7f, 0x80]));
    sets.push(of("abz"));
    sets.push(of([0x0f, 0x10, 0x8f]));
    sets.push(not(of(" ")));
    sets.push(not(of(" \"")));
    sets.push(not(of(" \"q")));
    sets.push(range('0', '9'));
    sets.push(range(0x70, 0x8f));
    sets.push(range(0x80, 0xff));
    sets.push(not(range('a', 'z')));
    sets.push(not(range(0, 0x7f)));
    sets.push(of("GPgp"));
    sets.push(of([0, 0x7f, 0x80, 0xff, 'x']));
    var column: bool[256] = [false; 256];     // the same low nibble in every row
    for r in 16 { column[r * 16 + 9] = true; }
    sets.push(column);
    var evens: bool[256] = [false; 256];
    for b in 128 { evens[2 * b] = true; }
    sets.push(evens);
    var most = not(of("GPgp"));
    most[0x90] = false;
    sets.push(most);
    var prepared: ByteSet[>..] = [];
    var kinds: u8[>..] = [];
    for &m in sets {
        let s = byte_set(m);
        format(kinds, " ", s.kind, if s.invert { "i" } else { "" });
        prepared.push(s);
    }
    print("kinds:", kinds);

    // find_any's 64-byte steps (on AArch64) start at 64; find_pair has none.
    let lens = [16, 17, 31, 32, 33, 47, 64, 79];
    let starts = [0, 3];
    var anys = 0;
    var pairs = 0;
    var found = 0;
    var wrong = 0;
    for &m, k in sets {
        let fill = outside(m);
        // The second set of the pair searches, and one of its members.
        let k2 = (k * 5 + 1) % sets.len;
        var member = 0;
        while !sets[k2][member] { member += 1; }
        for n in lens {
            for st in starts {
                var buf: u8[>..] = [];
                for i in st + n + 9 { buf.push(fill as! u8); }
                for pos in n {
                    for b in 256 {
                        buf[st + pos] = b as! u8;
                        let s = buf[st..st + n];
                        let want = plain_any(s, m);
                        if find_any(s, prepared[k]) != want {
                            wrong += 1;
                            print("any ", k, " ", n, " ", st, " ", pos, " ", b);
                        }
                        anys += 1;
                        if want >= 0 { found += 1; }
                        if st == 0 && n < 64 {
                            for d in [1, 9] {
                                let keep = buf[pos + d];
                                buf[pos + d] = member as! u8;
                                let s2 = buf[..n];
                                let pw = plain_pair(s2, m, d, sets[k2]);
                                if find_pair(s2, prepared[k], d, prepared[k2]) != pw {
                                    wrong += 1;
                                    print("pair ", k, " ", k2, " ", d, " ", n, " ", pos, " ", b);
                                }
                                pairs += 1;
                                if pw >= 0 { found += 1; }
                                buf[pos + d] = keep;
                            }
                        }
                    }
                    buf[st + pos] = fill as! u8;
                }
            }
        }
    }
    print(anys, " find_any and ", pairs, " find_pair searches, ", found, " found, ", wrong,
          " wrong");
}
