// abort(msg) ends the program with the message on stderr and a nonzero
// exit (§9.3); the output before it must still appear. Expected to abort.

fn checked_div(a: i64, b: i64) -> i64 {
    guard b != 0 else { abort("division by zero requested"); }   // Diverges (§6.4).
    a / b
}

fn main() {
    print(checked_div(7, 2));            // 3
    let msg: u8[] = "custom failure";
    print(msg.len);                      // 14
    if false { exit(3); }                // Never taken; typechecks and compiles.
    abort(msg);                          // -> stderr: goose runtime error: custom failure
    print(0);                            // Not reached.
}
