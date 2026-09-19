// An integer match reads a reference scrutinee as its pointee (§3.8, §8.1)
// and compares at the pointee's type: a reference variable, a written &x, a
// narrowed optional, a reference parameter (inlined at -O2), an untyped
// parameter bound to a reference, a const reference, a reference field, an
// element of an array of references, a returned reference, and a for
// binding by reference over i32 elements and over varint elements, which
// decode to i64. The scrutinee is read once, before its arm runs.
struct S { q: varint }
struct H { r: i64& }

let W = 7;

fn classify(r: u8&) -> i64 {
    match r { 0 => 0, 1..100 => 1, 100..200 => 2, _ => 3 }
}

fn kind(x) -> i64 { match x { 0 => 0, 1..10 => 1, _ => 2 } }

fn at(xs: i64[]&, i: i64) -> i64& { return xs[i]; }

fn main() {
    var a = 3;
    let r: i64& = a;
    print(match r { 3 => "three", _ => "other" });          // three
    print(match &a { 3 => "three", _ => "other" });         // three
    match r { 3 => print("statement"), _ => {} }            // statement
    let w = match r { 3 => { a = 4; r * 10 }, 4 => -1, _ => 0 };
    print(w, " ", a);                                       // 40 4

    var o: i64? = a;
    if o { print(match o { 0..4 => "low", 4..8 => "mid", _ => "high" }); }   // mid

    var b: u8 = 150;
    let rw: const i64& = W;
    print(classify(b), " ", kind(&a), " ", kind(a), " ", match rw { 7 => 1, _ => 0 });
                                                            // 2 1 1 1

    // Unsigned and signed pointees compare at their own type.
    var big: u64 = 9223372036854775808;
    let rb: u64& = big;
    var n: i8 = -5;
    let rn: i8& = n;
    print(match rb { 0..10 => "small", 10..18446744073709551615 => "large", _ => "max" }, " ",
          match rn { -128..0 => "negative", 0 => "zero", _ => "positive" });
                                                            // large negative

    let h = H { r: a };
    let ys: i64[] = [5, 6, 7];
    let refs: (i64&)[2] = [a, ys[2]];
    print(match h.r { 4 => "four", _ => "?" }, " ", match refs[1] { 7 => "seven", _ => "?" },
          " ", match at(ys, 1) { 6 => "six", _ => "?" });   // four seven six

    var xs: i32[3] = [1, 2, 3];
    for &x in xs { match x { 1 => { x = 10; }, 2..4 => { x += 1; }, _ => {} } }
    print(xs);                                              // [10, 3, 4]

    let v: varint[] = [3, 300, -3];
    for x in v { print(match x { 3 => "three", 300 => "300", -3 => "minus three", _ => "?" }); }
                                                            // three 300 minus three
    let s = S { q: 300 };
    print(match s.q { 300 => "300", _ => "?" });            // 300
}
