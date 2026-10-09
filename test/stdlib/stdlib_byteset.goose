// stdlib/std.goose's byte sets: find_any and find_pair against plain loops,
// over sets of every kind byte_set makes (the empty and the full set, one to
// three bytes and their complements, ranges and their complements, and sets
// only the tables describe), at every length up to 40 and some beyond, from
// several starts. That covers searches shorter than the runtime's blocks of
// 16 bytes, whole blocks, and the overlapping last block, on each path the
// target has. Then a few results worked out by hand.
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

fn main() {
    var sets: (bool[256])[>..] = [];
    sets.push([false; 256]);
    sets.push([true; 256]);
    sets.push(of("a"));
    sets.push(of("ab"));
    sets.push(of("abz"));
    sets.push(of([0xff]));
    sets.push(not(of(" ")));
    sets.push(not(of(" \"")));
    sets.push(not(of(" \"q")));
    sets.push(range('0', '9'));
    sets.push(range(0x80, 0xff));
    sets.push(not(range('a', 'z')));
    sets.push(of("GPgp"));
    sets.push(of([0, 0x7f, 0x80, 0xff, 'x']));
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

    // Mostly bytes outside the sets, mostly bytes inside, and a mix.
    let some: u8[] = "Gq0a\"x 9b\xffz\x80p\x00 \x7fP.g";
    var texts: (u8[])[>..] = [];
    for t in 3 {
        var text: u8[>..] = [];
        var r = rng { t as u64 + 1 };
        for i in 120 {
            let c = some[rand_int(r, some.len)];
            if t == 0 { text.push(if rand_int(r, 9) == 0 { c } else { 'q' }); }
            else if t == 1 { text.push(if rand_int(r, 9) == 0 { c } else { ' ' }); }
            else { text.push(c); }
        }
        texts.push(copy(text));
    }

    let starts = [0, 1, 3, 15, 16];
    var lens: i64[>..] = [];
    for n in 41 { lens.push(n); }
    lens.append([47, 48, 49, 63, 64, 65, 100]);
    var anys = 0;
    var pairs = 0;
    var found = 0;
    var wrong = 0;
    for text in texts {
        for st in starts {
            for n in lens {
                guard st + n <= text.len else { continue; }
                let s = text[st..st + n];
                for &m, k in sets {
                    let want = plain_any(s, m);
                    if find_any(s, prepared[k]) != want {
                        wrong += 1;
                        print("any ", k, " ", st, " ", n);
                    }
                    anys += 1;
                    if want >= 0 { found += 1; }
                    for d in [0, 1, 2, 17] {
                        let k2 = (k * 7 + d) % sets.len;
                        let pw = plain_pair(s, m, d, sets[k2]);
                        if find_pair(s, prepared[k], d, prepared[k2]) != pw {
                            wrong += 1;
                            print("pair ", k, " ", k2, " ", d, " ", st, " ", n);
                        }
                        pairs += 1;
                        if pw >= 0 { found += 1; }
                    }
                }
            }
        }
    }
    print(anys, " find_any and ", pairs, " find_pair searches, ", found, " found, ", wrong,
          " wrong");

    let comma = byte_set(",");
    print(find_any("hello, world", comma), " ", find_any("hello world", comma), " ",
          find_any("", comma));                                                 // 5 -1 -1
    let digits = byte_set(range('0', '9'));
    print(find_any("abcdefghijklmnopqrstuvwxyz ABCDEFGHIJ 7", digits));        // 38
    let a = byte_set("a");
    let b = byte_set("b");
    let d = byte_set("d");
    let ab = byte_set("ab");
    print(find_pair("abcabd", a, 2, d), " ", find_pair("abcabd", a, 0, ab), " ",
          find_pair("abcabd", d, 1, a), " ", find_pair("ab", a, 2, b));          // 3 0 -1 -1
    let line = "GET /index.html HTTP/1.1 200 2326 POST /api/data?token=abcdef123456 HTTP/1.1";
    let p = byte_set("Pp");
    let o = byte_set("Oo");
    let slash = byte_set("/");
    let lower_p = byte_set("p");
    print(find_pair(line, p, 1, o), " ", find_pair(line, slash, 2, lower_p));   // 34 39
}
