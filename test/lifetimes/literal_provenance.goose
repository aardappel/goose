// Array literals keep references rooted at the caller through every form.
struct Holder { value: i64& }

fn inferred(value: i64&) -> Holder[1] {
    let holders = [Holder { value: value }];
    return holders;
}

fn filled(value: i64&) -> Holder[2] { [Holder { value: value }; 2] }

fn nested(value: i64&) -> Holder[1][1] {
    let holders = [[Holder { value: value }]];
    return holders;
}

fn empty() -> Holder[0] {
    var local = 11;
    [Holder { value: local }; 0]
}

fn main() {
    var value = 7;
    let one = inferred(value);
    let two = filled(value);
    let outer = nested(value);
    value = 9;
    print(one[0].value);
    print(two[1].value);
    print(outer[0][0].value);
    print(empty().len);
}
