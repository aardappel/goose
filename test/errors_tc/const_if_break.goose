// error: break cannot cross a const for expansion boundary
fn main() { loop { const for <C> in [1] { const if true { break; } } } }
