// A continue leaves the loop body through restores of its own, for exactly
// the locals declared before it: a local declared after it never ran, so
// its base holds nothing to restore. Ordinary fallthrough still restores
// everything at the end of the body.

fn skip_before() {
    var xs: i64[>..] = [0, 1, 0, 2];
    for x in xs {
        if x == 0 { continue; }
        let s: u8[] = "abc";
        print(s, " ", x);                // abc 1, abc 2
    }
}

fn skip_between() {
    for x in 1..6 {
        var a: i64[>..] = [x, x * 10];
        if x == 1 { continue; }
        let b: u8[] = "b";
        var c: u8[>..] = "c";
        c.push(120);
        guard x != 4 else { continue; }
        print(a[1], " ", b, " ", c, " ", x);   // 20 b cx 2, 30 b cx 3, 50 b cx 5
    }
}

fn nested() {
    for i in 3 {
        let outer: u8[] = "o";
        var seen: i64[>..] = [];
        for j in 4 {
            var inner: i64[>..] = [j];
            if j == 0 { continue; }
            {
                let blk: u8[] = "blk";
                if j == 1 { continue; }      // Leaves the inner loop from a plain scope.
                seen.push(inner[0]);
            }
            match j {
                2 => {
                    let m: u8[] = "m";
                    if i == 1 { continue; }  // And from a match arm.
                    print(m, i, j);          // m02, m22
                },
                _ => {},
            }
        }
        if i == 0 { continue; }
        print(outer, " ", seen.len, " ", i); // o 2 1, o 2 2
    }
}

fn while_and_loop() {
    var i = 0;
    while i < 6 {
        i++;
        let w: u8[] = "w";
        if i % 2 == 0 { continue; }
        print(w, i);                         // w1, w3, w5
    }
    var n = 0;
    var total = 0;
    loop {
        n++;
        if n > 5 { break; }
        var v: i64[>..] = [n, n];
        if n == 3 { continue; }
        let t: u8[] = "t";
        total += v[0] + v[1];
        print(t, n);                         // t1, t2, t4, t5
    }
    print(total);                            // 24
}

// Every iteration allocates a large local and abandons it through continue;
// the reserve per stack is far smaller than the sum, so each continue must
// hand the watermark back.
fn no_growth() {
    var count = 0;
    for i in 5000 {
        var big: u8[>..] = [0; 65536];
        big.push(1);
        if big.len > i {
            count++;
            continue;
        }
        print("unreachable");
    }
    print(count);                            // 5000
}

fn main() {
    skip_before();
    skip_between();
    nested();
    while_and_loop();
    no_growth();
}
