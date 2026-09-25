// error: its storage does not outlive this function: return copy(f.marks)
// A function whose result type is inferred returns a non-fixed lvalue by
// reference (§4.1, §7.1), except its own local, which the return moves. A
// part of that local dies with the function, so it is neither returned by
// reference nor copied implicitly.
struct Frame { depth: i64, marks: i64[>..] }
fn marks() {
    var f = Frame { depth: 1, marks: [1] };
    return f.marks;
}
fn main() { print(marks()); }
