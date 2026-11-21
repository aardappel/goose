// An `export fn` that C calls back into, before the call returns, runs at
// gs_sp 0 and reads data-stack tops from memory. The frames above it keep
// those tops in locals, so a callback that allocates (a str()) overwrites
// them unless every frame published its tops first (issue 322). The externs
// here are the exports' own C wrappers: that is
// the call C makes, from main, from a function main called, and from a
// callback that calls back into C.

extern fn call_back(n: i64) -> i64 = "on_callback";
extern fn call_again(n: i64) -> i64 = "on_again";

export "on_callback" fn on_callback(n: i64) -> i64 {
    let s = str("callback temporary number ", n, " with some padding to fill the stack");
    let t = str(s, s, s);
    // Two more strings, so the callback's stacks reach a caller's own
    // stack and not only the ones main holds.
    let u = str(t, " pad");
    let v = str(u, " pad");
    return v.len;
}

// Its own string has to survive the nested call into C.
export "on_again" fn on_again(n: i64) -> i64 {
    let s = str("again pad ", n, " xxxxxxxxxxxxxxxx");
    let inner = call_back(n);
    if s != str("again pad ", n, " xxxxxxxxxxxxxxxx") { print("CORRUPTED again ", s.len); }
    return inner + s.len;
}

fn walk(keys: const u8[:][]&) -> i64 {
    let local = str("walk local ", 7);
    var total = 0;
    for k in keys {
        total += call_back(k.len);
        if local != "walk local 7" { print("CORRUPTED walk ", local.len); }
    }
    return total;
}

fn main() {
    let keys: const u8[:][] = ["alpha", "beta", "gamma", "delta"];
    let local = str("main local ", 42);
    var total = 0;
    for k in keys {
        total += call_back(k.len);
        if local != "main local 42" { print("CORRUPTED local ", local.len); }
        print(k, " ", local);
    }
    print("total ", total);
    print("walk ", walk(keys));
    if local != "main local 42" { print("CORRUPTED main ", local.len); }
    print("main still ", local);
    print("again ", call_again(3));
    if local != "main local 42" { print("CORRUPTED after again ", local.len); }
    print("main still ", local);
}
