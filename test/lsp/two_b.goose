// lsp: fixture for test/lsp_protocol.py: an entry file importing
// two_shared.goose, the only one instantiating its apply.
import two_shared;

fn main() {
    print(two_shared::twice(2));
    print(two_shared::apply(3.0));
}
