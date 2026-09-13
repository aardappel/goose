// error: cannot assign through this path (const, or a read-only instantiation, §9.5)
fn main() {
    let view = block { break "abc"; };
    view[0] = 'X';
}
