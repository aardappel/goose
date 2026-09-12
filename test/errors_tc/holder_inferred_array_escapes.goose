// Inferred array literals retain the lifetime of references in their elements.
struct Holder { value: i64& }
fn leak() -> Holder[1] {
    var value = 11;
    let holders = [Holder { value: value }];
    return holders;
}
fn main() { let holders = leak(); print(holders[0].value); }
