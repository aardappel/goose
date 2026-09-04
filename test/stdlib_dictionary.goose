// stdlib/dictionary.goose: integer, slice and struct keys, growth, deletion.
import dictionary;

struct key { a: i64, b: i64 }
fn hash(k: key) -> u64 { hash_combine(hash(k.a), hash(k.b)) }

fn main() {
    var d = dictionary<i64, i64> {};
    print(d.insert(1, 10), " ", d.insert(2, 20), " ", d.insert(1, 11));   // true true false
    print(d.count, " ", d.contains(1), " ", d.contains(3));               // 2 true false
    // A reference from get may not be in scope at the next insertion (the
    // checker enforces it), so each lives in its own block.
    block { let v = d.get(1); if v { print(v); } }                        // 11
    block { let w = d.get(3); print(w == null); }                         // true
    d.update(2, 0) { it += 5; };
    d.update(7, 0) { it += 5; };
    block { let v2 = d.get(2); if v2 { print(v2); } }                     // 25
    block { let v7 = d.get(7); if v7 { print(v7); } }                     // 5
    print(d.get_or_insert(9, 90), " ", d.get_or_insert(9, 91));           // 90 90
    print(d.remove(1), " ", d.remove(1), " ", d.count, " ", d.contains(1));   // true false 3 false
    var total = 0;
    d.each() { k, v => total += k + v; };
    print(total);                                                         // 138
    for i in 1000 { d.insert(i * 7, i); }
    print(d.count);                                                       // 1002
    var ok = true;
    for i in 1000 {
        let g = d.get(i * 7);
        if g { if g != i { ok = false; } } else { ok = false; }
    }
    print(ok);                                                            // true
    d.clear();
    print(d.count, " ", d.contains(14));                                  // 0 false
    var sd = dictionary<u8[:], i32> {};
    let text = "the cat and the dog and the bird";
    each_split(text, ' ') { sd.update(it, 0) { it += 1; } };
    block { let the = sd.get("the"); if the { print(the); } }             // 3
    block { let and = sd.get("and"); if and { print(and); } }             // 2
    print(sd.count);                                                      // 5
    var kd = dictionary<key, u8[:]> {};
    kd.insert(key { 1, 2 }, "x");
    kd.insert(key { 2, 1 }, "y");
    block { let kx = kd.get(key { 1, 2 }); if kx { print(kx); } }         // x
    print(kd.contains(key { 2, 1 }), " ", kd.contains(key { 1, 1 }));      // true false
    var rs = dictionary<i64, bool> {};
    rs.reserve(100);
    print(rs.slots.len >= 150, " ", rs.count);                            // true 0
}
