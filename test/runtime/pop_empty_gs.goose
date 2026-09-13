// Pop on an empty grow-shrink array is a runtime abort (§9.3): the array
// tops its data stack, so an unchecked pop would move the stack top below
// the element base and hand that memory to values living underneath it.
// The program ends in that abort, so nothing follows it.

fn main_pop_empty_gs() {
    var a: i64[>..<] = [1, 2];
    print(a.pop());                      // 2
    print(a.pop());                      // 1
    print(a.len);                        // 0
    print(a.pop());                      // Aborts.
}

fn main() { main_pop_empty_gs(); }
