// A body that still calls a nested function is inlined all the same: the
// copied call passes the copies of the variables the nested function
// reaches (§7.5). The nested functions return two values, so they are never
// inlined themselves, except `peek`, which is once its caller has been.

// Used once, with a captured local, a captured parameter bound to a literal,
// and a captured resizable the nested function grows.
fn tally(n: i64, step: i64) -> i64 {
    var count = 0;
    var seen: i64[>..] = [];
    fn note(k: i64) -> i64, bool {
        count += step;
        seen.push(k);
        return count, true;
    }
    for i in n { note(i); }
    return count * 100 + seen.len;
}

// Used three times: three copies, each passing its own variables.
fn twice(a: i64) -> i64 {
    var acc = a;
    fn add(k: i64) -> i64, i64 {
        acc += k;
        return acc, k;
    }
    add(a);
    add(1);
    return acc;
}

// Inlined into `outer`, which is inlined into main: the call to `add2`
// passes the copy of a copy.
fn inner(x: i64) -> i64 {
    var total = x;
    fn add2(k: i64) -> i64, i64 {
        total += k;
        return total, 0;
    }
    add2(10);
    add2(20);
    return total;
}
fn outer(x: i64) -> i64 { inner(x) + inner(x + 1) }

// `peek` is small, but `look` calls it from a loop body nested two blocks
// deep; once `look` is inlined into main, `peek` is inlined there too and
// reads the copy `look`'s call to it passed.
fn look(v: i64) -> i64 {
    var cur = v;
    fn peek() -> i64 { cur * 2 }
    fn bump() -> i64, i64 {
        cur += 1;
        return cur, 0;
    }
    var s = 0;
    for i in 3 {
        bump();
        s += peek();
    }
    return s;
}

// The nested function writes the index the caller tested: the test's fact
// does not survive the call, in the copy either.
fn after(a: i64[:], start: i64) -> i64 {
    var i = start;
    fn move_to(k: i64) -> i64, i64 {
        i = k;
        return i, 0;
    }
    if i >= 0 && i < a.len {
        move_to(a.len - 1);
        return a[i];                                // bce:keep
    }
    return -1;
}

fn main() {
    print(tally(4, 3));                            // 12 * 100 + 4
    print(twice(5), " ", twice(7), " ", twice(0));  // 11 15 1
    print(outer(1));                               // 31 + 32
    print(look(4));                                // 10 + 12 + 14
    let a = [3, 1, 4, 1, 5];
    print(after(a, 0), " ", after(a, 9));           // 5 -1
}
