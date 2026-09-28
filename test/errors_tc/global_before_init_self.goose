// error: variable S may be used before it is assigned: globals initialize in declaration order
// error: global_before_init_self.goose:4 has run
// A global has no value while its own initializer runs (§11.1).
var S: const u8[:] = S[..];
fn main() { print(S); }
