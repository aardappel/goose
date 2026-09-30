// A value whose references lead back into it never finishes rendering
// (§3.7): every level of a type that reaches itself through references is
// rendered by a call of its own, until the native stack runs out, a safe
// abort (§9.3). Expected to abort.

struct N { v: i64, next: N? }
var n1 = N { v: 1, next: null };
var n2 = N { v: 2, next: null };

fn main() {
    n1.next .= n2;
    n2.next .= n1;
    print("cycle");
    print(n1);                  // Aborts.
}
