export fn hello_world() -> const u8[:] {
    return "Hello, world!"
}

export "c_add" fn add(a: i32, b: i32) -> i32 {
    return a + b;
}

fn main() {
    print(hello_world(), " ", add(20, 22));
}
