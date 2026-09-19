// error: binding r to a reference rooted at a temporary, which does not outlive it
// `.=` binds a reference to its initializer: here a field of a call's result.
struct Box { body: u8[] }
fn mk() -> Box { Box { body: "hello" } }
fn main() {
    let r .= mk().body;
    print(r);
}
