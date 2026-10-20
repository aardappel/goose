// error: extern fn must be declared at top level
fn main() {
    extern "SDL2" {
        fn SDL_Init(flags: u32) -> i32;
    }
}
