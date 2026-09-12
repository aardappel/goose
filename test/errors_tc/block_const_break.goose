fn main() {
    let view = block { break "abc"; };
    view[0] = 'X';
}
