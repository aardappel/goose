// error: name is a var of type const u8[:], which does not fix its length, while the array size at
// error: const_len_var_before.goose:5 relies on it keeping its initializer's
// §3.3: a size checked before the var's declaration is checked against its
// type there.
struct Word { letters: u8[name.len] }

var name = "goose";

fn main() {
    let w = Word { "geese" };
    print(w, name);
}
