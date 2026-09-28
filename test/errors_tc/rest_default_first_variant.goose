// error: missing initializer for field route of Trip (it has no declared default, and field to has no declared default and Stop& is a non-optional reference)
// A literal's `..` fills a field of a variable-mode ADT with its first
// variant, whose payload it fills the same way (§4.2): the strings there
// get `[]`, but a non-optional reference has no default value.
struct Stop { name: u8[] }
enum Route { Direct { to: Stop&, via: u8[][] }, Nowhere }
struct Trip { day: i64, route: Route.., notes: u8[] }

fn main() {
    let t = Trip { day: 1, .. };
    print(t.day);
}
