// runtime-define: GS_STACK_RESERVE=65536
// Appending defaults up to an end no data stack can hold runs as one bulk
// clear, and reports the overrun the per-element pushes would have hit.
fn main() {
    var a: u8[>..] = [];
    let n = 1000000;
    for i in a.len..n { a.push(default<u8>()); }
    print(a.len);
}
