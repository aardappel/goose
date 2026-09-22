// runtime-define: GS_MAX_STACKS=4
// Functions of a recursive cycle own non-fixed locals whose scope ends
// before they call back into the cycle (§7.8). A local's data stack is free
// again once its block ends, so every activation reuses the same few
// stacks: a thousand levels run within four, where a stack per level would
// abort.

enum Tok { Num { v: i64 }, Word { s: u8[] } }
struct Frame { depth: i64, marks: i64[>..] }     // a frame object

// The scratch in a block that closes before the self-call.
recursive fn in_block(n: i64) -> i64 {
    var got = 0;
    {
        var scratch: i64[>..] = [];
        for i in 3 { scratch.push(n + i); }
        got = scratch[2] - scratch[0];
    }
    if n == 0 { return got; }
    let below = in_block(n - 1);
    return got + below;
}

// A loop body's scratch, gone before the call after the loop.
recursive fn in_loop(n: i64) -> i64 {
    var sum = 0;
    for i in 2 {
        var parts: u8[>..] = [];
        parts.append("ab");
        sum += parts.len;
    }
    if n == 0 { return sum; }
    let below = in_loop(n - 1);
    return sum + below;
}

// Mutual recursion, with a scratch of its own in each function.
recursive fn even(n: i64) -> bool {
    {
        var s: i64[>..<] = [];
        s.push(n);
        s.push(n + 1);
        s.pop();
        if s.len != 1 { return false; }
    }
    if n == 0 { return true; }
    return odd(n - 1);
}
fn odd(n: i64) -> bool {
    {
        let t: u8[] = str(n);
        if t.len == 0 { return true; }
    }
    if n == 0 { return false; }
    return even(n - 1);
}

// Through a nested function, whose scratch closes before it calls back.
recursive fn outer(n: i64) -> i64 {
    fn helper(k: i64) -> i64 {
        {
            var tmp: i64[>..] = [];
            tmp.push(k);
            if tmp.len != 1 { return -1; }
        }
        let r = outer(k - 1);
        return r + 1;
    }
    if n == 0 { return 0; }
    return helper(n);
}

// Through a function value's body, the same way.
fn apply<F>(x: i64) -> i64 { return F(x); }
recursive fn through_block(n: i64) -> i64 {
    if n == 0 { return 0; }
    let r = apply(n) { k =>
        {
            var tmp: u8[>..] = [];
            tmp.append("xyz");
            if tmp.len != 3 { return -1; }
        }
        through_block(k - 1)
    };
    return r + 1;
}

// A variable-mode value, and a match arm's copy of its payload.
recursive fn matched(n: i64) -> i64 {
    var got = 0;
    {
        let t: Tok.. = if n % 2 == 0 { Tok.Word { s: str("w", n) } } else { Tok.Num { v: n } };
        got = match t { Word w => w.s.len, Num x => 1 };
    }
    if n == 0 { return got; }
    let below = matched(n - 1);
    return got + below;
}

// A frame object, and a reusable pool with its freelist's stack.
recursive fn framed(n: i64) -> i64 {
    var got = 0;
    {
        var f = Frame { depth: n, marks: [] };
        f.marks.push(n);
        f.marks.push(n + 1);
        reusable var slots: i64[>..] = [];
        let a = slots.alloc_index(n);
        slots.free(a);
        let b = slots.alloc_index(n + 2);
        got = f.marks.len + slots.len + b - a;
    }
    if n == 0 { return got; }
    let below = framed(n - 1);
    return got + below;
}

// Declared after the call in a loop body: each iteration's local ends
// before the next iteration calls.
recursive fn after_call(n: i64) -> i64 {
    var total = 0;
    var i = 0;
    while i < 2 {
        if n > 0 && i == 0 { total += after_call(n - 1); }
        var late: i64[>..] = [];
        late.push(i);
        total += late.len;
        i += 1;
    }
    return total;
}

fn main() {
    let depth = 1000;
    print(in_block(depth));                  // 2 per level
    print(in_loop(depth));                   // 4 per level
    print(even(depth));
    print(outer(depth));
    print(through_block(depth));
    print(matched(depth));                   // "w" and the digits, or 1
    print(framed(depth));                    // 3 per level
    print(after_call(depth));                // 2 per level
}
