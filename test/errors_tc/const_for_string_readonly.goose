// error: cannot assign through this path (const, or a read-only instantiation
fn main() { const for <C> in ["hi"] { C[0] = 0; } }
