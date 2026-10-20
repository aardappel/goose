// error: an extern block names the library it binds from
extern {
    fn SDL_Init(flags: u32) -> i32;
}
fn main() {}
