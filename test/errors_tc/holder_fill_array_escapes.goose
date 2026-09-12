// Fill arrays retain the lifetime of references in the repeated element.
struct Holder { value: i64& }
fn leak() -> Holder[2] {
    var value = 11;
    return [Holder { value: value }; 2];
}
fn main() { let holders = leak(); print(holders[0].value); }
