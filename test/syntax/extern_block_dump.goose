// parse-only: an extern block's members dump back inside the block (§7.10).
extern "SDL2" {
    fn SDL_Init(flags: u32) -> i32;
    fn SDL_Quit();
    fn ticks() -> u64 = "SDL_GetTicks64";
    fn gfx::delay(ms: u32 = 16);
}
extern fn sqrt(x: f64) -> f64;
extern "SDL2" {
    fn SDL_GetError() -> u64;
}

// A block's library and a member's symbol dump with their escapes (§7.10).
extern "bin\\example" {
    fn answer() -> i32 = "an\\swer";
}
