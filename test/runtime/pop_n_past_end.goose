// pop_n of more elements than the array holds is a runtime abort (§9.3):
// it shrinks past empty, as a pop on an empty array does.

fn main_pop_n_past_end() {
    var a: i64[>..<] = [1, 2, 3];
    a.pop_n(3);
    print(a.len);                        // 0
    a.push(4);
    a.pop_n(2);                          // Aborts.
}

fn main() { main_pop_n_past_end(); }
