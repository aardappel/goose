// A guard guards the rest of the block it is in, whatever kind of block that
// is: `guard c; rest` is `if c { rest }`, and `guard c else { s } rest` is
// `if c { rest } else { s }` (§6.4). It jumps nowhere: in a function value's
// body it ends that call of the value, not the function running the value
// nor the named function around it, and in a loop's body it goes on to the
// next iteration. Leaving more than the block takes an else that does.

fn each<F>(k: i64) {
    for i in k { F(i); }
    print("each done");
}
fn each_via<F>(k: i64) {
    each(k, F);
    print("each_via done");
}

// The block's guard skips the rest of the block only: each runs every
// iteration, and setn goes on to assign n however d is.
fn run(c: bool) {
    var n: i64;
    fn setn(d: bool) {
        each(2) { guard !d; print("block ", it); }
        n = 5;
    }
    setn(c);
    print(n);
}

// A HOF's result is its own, whatever the value's body does.
fn first<F>(k: i64) {
    for i in k { F(i); }
    return 7;
}

fn loops() {
    for i in 5 { guard i % 2 == 0; print("even ", i); }
    var j = 0;
    while j < 5 { j++; guard j != 3; print("while ", j); }
    var k = 0;
    loop { k++; guard k < 3 else { break; } print("loop ", k); }
}

fn scopes(c: bool) {
    if c { print("then"); guard !c; print("never"); }
    { guard !c; print("scope"); }
    print("after ", c);
    var m: i64;
    { guard c else { m = 1; } m = 2; }
    let v = block { guard c else { break 1; } 2 };
    let w = { guard c else { 3 } 4 };
    print(m, " ", v, " ", w);
}

// An else block that does not leave runs instead of the rest of its block.
fn fallback(n: i64) {
    guard n > 0 else { print("not positive: ", n); }
    print("positive: ", n);
}

// Directly in a function's body the rest of the block is the rest of the
// function, and the if the guard is gives the value its tail does.
fn half(n: i64) -> i64 { guard n % 2 == 0 else { return -1; } n / 2 }

fn tenfold(p: i64?) -> i64 { guard p else { return 0; } p * 10 }

fn main() {
    run(false);
    run(true);
    each_via(3) { guard it != 1; print("via ", it); };
    let r = first(3) { guard it > 0; print("first ", it); };
    print(r);
    loops();
    scopes(true);
    scopes(false);
    fallback(3);
    fallback(-3);
    print(half(8), " ", half(7));
    var y = 4;
    print(tenfold(y), " ", tenfold(null));
}
