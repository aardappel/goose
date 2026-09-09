// A namespaced declaration is not visible unqualified from the global
// namespace; only its own namespace sees it by its leaf name.
struct img::Pixel { r: u8 }
fn img::bright(p: Pixel) -> i64 { p.r }
fn main() {
    let p = img::Pixel { 1 };
    print(bright(p));
}
