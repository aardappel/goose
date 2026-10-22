// error: resizable field data of struct S must be the final field, with no pad after it
// A resizable field must be the final field (§3.4), and a pad after it is a
// field too: its bytes would lie where the tail grows.
struct S { n: i64, data: i64[>..], pad 4 }

fn main() {
    var s = S { n: 1, data: [2, 3] };
    s.data.push(4);
    print(s.n + s.data.len);
}
