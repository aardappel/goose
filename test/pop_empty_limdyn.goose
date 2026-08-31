// As pop_empty_lim, for the construction-time capacity form, whose length
// is a u32 in the value's byte layout rather than a field of a fixed-size
// C struct: a different length lvalue, the same wrap on an empty pop.

fn main_pop_empty_limdyn() {
    var d: i64[..] = [..3];
    d.push(7);
    print(d.pop());                      // 7
    print(d.len);                        // 0
    print(d.pop());                      // Aborts.
}

fn main() { main_pop_empty_limdyn(); }
