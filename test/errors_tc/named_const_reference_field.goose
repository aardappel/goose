// error: K is bound to a writable reference here, through which it may change, while the array size at
// error: named_const_reference_field.goose:7 relies on it keeping its initializer's value
// §11.1, §4.4: a reference field that is not `const` binds K by reference,
// and writing through the field writes K.

let K = 3;
struct Grid { cells: i64[K] }
struct Counter { n: i64& }

fn main() {
    var c = Counter { n: K };
    c.n += 1;
    let g = Grid { cells: [0; 3] };
    print(g.cells.len, " ", K);
}
