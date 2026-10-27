// error: defer is a statement: it runs its body when the block it is in ends
fn main() {
    let x = defer print("no");
}
