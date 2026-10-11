// A cached body owns its locals at every call depth. Rechecking a deeper
// loop must not store each new temporary into its earlier local holder.
import std;
struct Reader { text: const u8[:] }
fn length(text: const u8[:]) -> i64 {
    var r = Reader { text: text };
    return r.text.len;
}
fn deep() { for i in 0..3 { print(length(str(i))); } }
fn main() {
    var value: i64 = 42;
    print(length(str(value)));
    deep();
}
