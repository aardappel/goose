// error: extern fn cannot be checkfree
extern "SDL2" {
    checkfree fn SDL_Init(flags: u32) -> i32;
}
fn main() {}
