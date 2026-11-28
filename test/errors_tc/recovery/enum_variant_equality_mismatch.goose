// error: == requires operands of the same type
// error: == requires operands of the same type
// error: == requires operands of the same type
enum Flag { Off, On }
enum Switch { Off, On }
enum Box<T> { Value { value: T } }

fn unrelated() {
    let f: Flag = Flag.Off;
    print(f == Switch.Off {});
}

fn different_type_arguments() {
    let b: Box<i64> = Box<i64>.Value { value: 1 };
    print(b == Box<i32>.Value { value: 1 });
}

fn two_variants() {
    print(Flag.Off {} == Flag.On {});
}

fn main() {
    unrelated();
    different_type_arguments();
    two_variants();
}
