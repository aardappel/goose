// error: cannot infer the element type of []: == gives it none
// An operand of == takes no type from the other one, so neither [] has an
// element type to compare by (§4.2).
fn main() {
    print([] == []);
}
