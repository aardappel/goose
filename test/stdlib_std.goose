// stdlib/std.goose, one function group per section; the expected values
// are worked out by hand in the comments.
import std;

fn scalars() {
    print(min(3, 7), " ", max(3, 7), " ", clamp(15, 0, 10), " ", clamp(-2, 0, 10));   // 3 7 10 0
    print(abs(-4), " ", abs(-2.5), " ", abs(1.5));                                     // 4 2.5 1.5
    print(sign(-9), " ", sign(0), " ", sign(4), " ", sign(-0.5), " ", sign(2.0));        // -1 0 1 -1 1
    print(lerp(0.0, 10.0, 0.25));                                                      // 2.5
    print(next_pow2(0), " ", next_pow2(1), " ", next_pow2(5), " ", next_pow2(64));     // 1 1 8 64
    print(popcount(255), " ", popcount(0), " ", clz(1), " ", ctz(8), " ", clz(0));      // 8 0 63 3 64
    var a = 1;
    var b = 2;
    swap(a, b);
    print(a, " ", b);                                                                  // 2 1
}

fn hashing() {
    print(hash(1) != hash(2), " ", hash(7) == hash(7), " ", hash("abc") == hash("abc"), " ",
          hash("abc") != hash("abd"));                                          // true true true true
    print(hash_combine(hash(1), hash(2)) != hash_combine(hash(2), hash(1)));   // true
    print(hash(true) != hash(false), " ", hash(1.5) == hash(1.5));             // true true
}

fn random() {
    var r = rng { 42 };
    let a = rand_u64(r);
    let b = rand_u64(r);
    print(a != b);                                    // true
    var r2 = rng { 42 };
    print(rand_u64(r2) == a);                         // true
    var ok = true;
    for i in 100 {
        let v = r.rand_int(6);
        if v < 0 || v >= 6 { ok = false; }
        let f = r.rand_flt();
        if f < 0.0 || f >= 1.0 { ok = false; }
    }
    print(ok);                                        // true
    var xs = [1, 2, 3, 4, 5, 6, 7, 8];
    shuffle(xs, r);
    sort(xs);
    print(xs[0], " ", xs[7], " ", xs.len);            // 1 8 8
}

fn searching() {
    let xs = [5, 3, 9, 1, 7];
    var s = 0;
    each_rev(xs) { s = s * 10 + it; };
    print(s);                                                                  // 71935
    var chunks = 0;
    var tot = 0;
    each_chunk(xs, 2) { c, i => chunks += 1; tot += c.len * (i + 1); };
    print(chunks, " ", tot);                                                   // 3 9
    let f = find(xs) { it > 6 };
    if f { print(f); }                                                         // 9
    print(find_index(xs) { it == 1 }, " ", find_index(xs) { it == 2 });        // 3 -1
    print(position(xs, 7), " ", position(xs, 8), " ", contains(xs, 3), " ", contains(xs, 4));   // 4 -1 true false
    print(any(xs) { it > 8 }, " ", all(xs) { it > 0 }, " ", all(xs) { it > 1 }, " ",
          count(xs) { it > 4 });                                               // true true false 3
    print(min(xs), " ", max(xs), " ", min(xs) { a, b => b < a }, " ", max(xs) { a, b => b < a });   // 1 9 9 1
    print(min_by(xs) { -it }, " ", max_by(xs) { -it }, " ", last(xs));         // 2 3 7
    let sorted = [1, 3, 5, 7, 9];
    print(lower_bound(sorted) { it < 6 });                                     // 3
    let p, found = binary_search(sorted, 7);
    print(p, " ", found);                                                      // 3 true
    let q, found2 = binary_search(sorted, 4);
    print(q, " ", found2);                                                     // 2 false
    print(find("hello world", "o w"), " ", rfind("hello world", "o"), " ", find("abc", "x"));   // 4 7 -1
    print(starts_with("hello", "he"), " ", ends_with("hello", "lo"), " ", starts_with("hi", "hello"));   // true true false
}

fn transforming() {
    let xs = [1, 2, 3, 4];
    let ys = map(xs) { it * it };
    print(ys.len, " ", ys[3]);                                                 // 4 16
    let ev = filter(xs) { it % 2 == 0 };
    print(ev.len, " ", ev[0], " ", ev[1]);                                     // 2 2 4
    print(fold(xs, 100) { a, x => a + x }, " ", sum(xs), " ", sum([1.5, 2.5]));   // 110 10 4
    let c = concat(xs, ev);
    print(c.len, " ", c[5]);                                                   // 6 4
    var words: u8[][>..] = ["bb", "a", "ccc"];
    let lens = map(words) { it.len };
    print(lens[0], lens[1], lens[2]);                                          // 213
}

fn inplace() {
    var xs = [3, 1, 2];
    fill(xs, 7);
    print(xs[0], xs[1], xs[2]);                                                // 777
    var d = [0, 0, 0];
    copy_into(d, [4, 5, 6]);
    print(d[2]);                                                               // 6
    var r = [1, 2, 3, 4];
    reverse(r);
    print(r[0], r[3]);                                                         // 41
    var big: i64[>..] = [];
    var g = rng { 7 };
    for i in 100 { big.push(g.rand_int(1000)); }
    sort(big);
    var ok = true;
    for i in 99 { if big[i + 1] < big[i] { ok = false; } }
    print(ok, " ", big.len);                                                   // true 100
    var ss = [5, 2, 8, 1];
    sort(ss) { a, b => b < a };
    print(ss[0], ss[3]);                                                       // 81
    var st = [3, 1, 2, 1, 3];
    stable_sort(st);
    print(st[0], st[1], st[4]);                                                // 113
    var lo: u8[>..] = [];
    lo.append("Hello World");
    to_lower(lo);
    print(lo);                                                                 // hello world
    to_upper(lo);
    print(lo);                                                                 // HELLO WORLD
}

fn lengths() {
    var xs: i64[>..<] = [1, 2, 3];
    push_n(xs, 9, 2);
    print(xs.len, " ", xs[4]);                                                 // 5 9
    insert_at(xs, 1, 7);
    print(xs[1], xs[2], xs.len);                                               // 726
    print(remove_at(xs, 1), " ", xs[1], " ", xs.len);                          // 7 2 5
    print(swap_remove(xs, 0), " ", xs[0], " ", xs.len);                        // 1 9 4
    retain(xs) { it != 9 };
    print(xs.len, " ", xs[0], xs[1]);                                          // 2 23
    var dd: i64[>..<] = [1, 1, 2, 2, 2, 3];
    dedup(dd);
    print(dd.len, " ", dd[0], dd[1], dd[2]);                                   // 3 123
    var h: i64[>..<] = [];
    heap_push(h, 5);
    heap_push(h, 1);
    heap_push(h, 4);
    heap_push(h, 2);
    print(heap_pop(h), heap_pop(h), heap_pop(h), heap_pop(h));                 // 1245
    var mh: i64[>..<] = [9, 3, 7, 1, 5];
    make_heap(mh);
    print(heap_pop(mh), heap_pop(mh));                                         // 13
    var mx: i64[>..<] = [2, 9, 4];
    make_heap(mx) { a, b => b < a };
    print(heap_pop(mx) { a, b => b < a });                                     // 9
    var lim: i64[..4] = [1];
    push_n(lim, 2, 2);
    print(lim.len, lim[2]);                                                    // 32
}

fn strings() {
    var o: u8[>..] = [];
    format_int(o, 255, 16, 6, ' ');
    o.push('|');
    format_int(o, -42, 10, 5, '0');
    o.push('|');
    format_uint(o, 5, 2, 8, '0');
    print(o);                                                                  //     ff|-0042|00000101
    var f: u8[>..] = [];
    format_flt(f, 3.14159, 2);
    f.push(' ');
    format_flt(f, -2.5, 0);
    f.push(' ');
    format_flt(f, 0.005, 2);
    print(f);                                                                  // 3.14 -3 0.01
    let v, ok = parse_int("-123");
    print(v, " ", ok);                                                         // -123 true
    let v2, ok2 = parse_int("12x");
    print(v2, " ", ok2);                                                       // 0 false
    let v3, ok3 = parse_int("ff", 16);
    print(v3, " ", ok3);                                                       // 255 true
    let fl, okf = parse_flt("-1.5e2");
    print(fl, " ", okf);                                                       // -150 true
    let fl2, okf2 = parse_flt("abc");
    print(okf2);                                                               // false
    print(compare("abc", "abd"), " ", compare("b", "a"), " ", compare("a", "a"), " ",
          compare("ab", "abc"));                                               // -1 1 0 -1
    print(trim("  hi \n"), "|", trim_start("  x"), "|", trim_end("y  "), "|");   // hi|x|y|
    let parts = split("a,b,,c", ',');
    print(parts.len, " ", parts[0], parts[3], " ", parts[2].len);              // 4 ac 0
    var n = 0;
    each_split("one two three", ' ') { n += it.len; };
    print(n);                                                                  // 11
    var m = 0;
    each_split("a--b--c", "--") { m += 1; };
    print(m);                                                                  // 3
    var j: u8[>..] = [];
    join(j, parts, "+");
    print(j);                                                                  // a+b++c
    var rp: u8[>..] = [];
    format_replaced(rp, "the cat sat", "at", "og");
    print(rp);                                                                 // the cog sog
    var cps = 0;
    var last_cp = 0;
    each_utf8("héllo€") { cps += 1; last_cp = it; };
    print(cps, " ", last_cp);                                                  // 6 8364
    var u: u8[>..] = [];
    push_utf8(u, 8364);
    push_utf8(u, 233);
    push_utf8(u, 65);
    print(u.len, " ", u[0], " ", u[3], " ", u[5]);                             // 6 226 195 65
}

fn main() {
    scalars();
    hashing();
    random();
    searching();
    transforming();
    inplace();
    lengths();
    strings();
}
