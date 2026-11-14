//! Cykis - Tetris Clone for CykOS
//! Userland Rust application using int 0x80 syscalls.
//! Includes automatic block drop timer, keyboard input, full scoring, and audio chimes.

#![no_std]
#![no_main]

use core::panic::PanicInfo;

mod cykos;
use cykos::*;

// Board dimensions (standard 10 wide, 20 high)
const BOARD_W: usize = 10;
const BOARD_H: usize = 20;

// Board screen origin on 80x25 VGA text mode
const ORIGIN_X: u8 = 30;
const ORIGIN_Y: u8 = 2;

// Tetromino definitions (7 classic shapes, 4 rotations each, 4x4 bounding boxes)
// Represented as 16-bit bitmasks (4 rows x 4 cols)
static PIECES: [[u16; 4]; 7] = [
    // I piece
    [0x0F00, 0x2222, 0x00F0, 0x4444],
    // J piece
    [0x8E00, 0x6440, 0x0E20, 0x44C0],
    // L piece
    [0x2E00, 0x4460, 0x0E80, 0xC440],
    // O piece
    [0x6600, 0x6600, 0x6600, 0x6600],
    // S piece
    [0x6C00, 0x4620, 0x06C0, 0x8C40],
    // T piece
    [0x4E00, 0x4640, 0x0E40, 0x4C40],
    // Z piece
    [0xC600, 0x2640, 0x0C60, 0x4C80],
];

// Colors for each piece (VGA color attributes)
static PIECE_COLORS: [u8; 7] = [
    0x0B, // Cyan (I)
    0x09, // Blue (J)
    0x06, // Brown/Orange (L)
    0x0E, // Yellow (O)
    0x0A, // Light Green (S)
    0x0D, // Magenta (T)
    0x0C, // Light Red (Z)
];

// Simple LCG PRNG for freestanding game loop
static mut RNG_STATE: u32 = 0x12345678;

fn rand_piece() -> usize {
    unsafe {
        RNG_STATE = RNG_STATE.wrapping_mul(1103515245).wrapping_add(12345);
        ((RNG_STATE >> 16) % 7) as usize
    }
}

// Micro-delay using hardware msleep
fn delay_loop(count: u32) {
    if count > 0 {
        msleep((count / 1000).max(1));
    }
}

// Game State
struct GameState {
    board: [[u8; BOARD_W]; BOARD_H],
    cur_piece: usize,
    cur_rot: usize,
    cur_x: i32,
    cur_y: i32,
    score: u32,
    lines: u32,
    game_over: bool,
}

impl GameState {
    fn new() -> Self {
        let mut g = Self {
            board: [[0; BOARD_W]; BOARD_H],
            cur_piece: 0,
            cur_rot: 0,
            cur_x: 3,
            cur_y: 0,
            score: 0,
            lines: 0,
            game_over: false,
        };
        g.cur_piece = rand_piece();
        g
    }

    fn check_collision(&self, piece: usize, rot: usize, px: i32, py: i32) -> bool {
        let mask = PIECES[piece][rot % 4];
        for r in 0..4 {
            for c in 0..4 {
                let bit = (mask >> (15 - (r * 4 + c))) & 1;
                if bit != 0 {
                    let bx = px + c as i32;
                    let by = py + r as i32;
                    if bx < 0 || bx >= BOARD_W as i32 || by >= BOARD_H as i32 {
                        return true;
                    }
                    if by >= 0 && self.board[by as usize][bx as usize] != 0 {
                        return true;
                    }
                }
            }
        }
        false
    }

    fn lock_piece(&mut self) {
        let mask = PIECES[self.cur_piece][self.cur_rot % 4];
        let color = PIECE_COLORS[self.cur_piece];
        for r in 0..4 {
            for c in 0..4 {
                let bit = (mask >> (15 - (r * 4 + c))) & 1;
                if bit != 0 {
                    let bx = self.cur_x + c as i32;
                    let by = self.cur_y + r as i32;
                    if by >= 0 && by < BOARD_H as i32 && bx >= 0 && bx < BOARD_W as i32 {
                        self.board[by as usize][bx as usize] = color;
                    }
                }
            }
        }

        self.clear_lines();
        self.cur_piece = rand_piece();
        self.cur_rot = 0;
        self.cur_x = 3;
        self.cur_y = 0;

        if self.check_collision(self.cur_piece, self.cur_rot, self.cur_x, self.cur_y) {
            self.game_over = true;
        }
    }

    fn clear_lines(&mut self) {
        let mut cleared = 0;
        let mut r = (BOARD_H - 1) as i32;
        while r >= 0 {
            let mut full = true;
            for c in 0..BOARD_W {
                if self.board[r as usize][c] == 0 {
                    full = false;
                    break;
                }
            }
            if full {
                cleared += 1;
                for move_r in (1..=r as usize).rev() {
                    self.board[move_r] = self.board[move_r - 1];
                }
                self.board[0] = [0; BOARD_W];
            } else {
                r -= 1;
            }
        }

        if cleared > 0 {
            self.lines += cleared;
            let pts = match cleared {
                1 => 100,
                2 => 300,
                3 => 500,
                _ => 800,
            };
            self.score += pts;
            // Play success chime
            sound_chime();
        } else {
            // Soft drop tone
            sound_tone(220, 20);
        }
    }

    fn draw_cell(x: u8, y: u8, color: u8, ch: u8) {
        set_cursor(x, y);
        let mut buf = [ch, 0];
        let vga_ptr = 0xB8000 as *mut u16;
        let offset = (y as usize) * 80 + (x as usize);
        unsafe {
            *vga_ptr.add(offset) = ((color as u16) << 8) | (ch as u16);
        }
    }

    fn render(&self) {
        // Draw Border
        for y in 0..(BOARD_H + 2) {
            Self::draw_cell(ORIGIN_X - 1, ORIGIN_Y + y as u8, 0x07, b'|');
            Self::draw_cell(ORIGIN_X + (BOARD_W * 2) as u8, ORIGIN_Y + y as u8, 0x07, b'|');
        }
        for x in 0..(BOARD_W * 2 + 2) {
            Self::draw_cell(ORIGIN_X - 1 + x as u8, ORIGIN_Y + (BOARD_H + 1) as u8, 0x07, b'-');
        }

        // Draw Board content
        for r in 0..BOARD_H {
            for c in 0..BOARD_W {
                let cell = self.board[r][c];
                let sx = ORIGIN_X + (c * 2) as u8;
                let sy = ORIGIN_Y + 1 + r as u8;
                if cell != 0 {
                    Self::draw_cell(sx, sy, cell, b'[');
                    Self::draw_cell(sx + 1, sy, cell, b']');
                } else {
                    Self::draw_cell(sx, sy, 0x08, b'.');
                    Self::draw_cell(sx + 1, sy, 0x08, b' ');
                }
            }
        }

        // Draw Active Falling Piece
        let mask = PIECES[self.cur_piece][self.cur_rot % 4];
        let color = PIECE_COLORS[self.cur_piece];
        for r in 0..4 {
            for c in 0..4 {
                let bit = (mask >> (15 - (r * 4 + c))) & 1;
                if bit != 0 {
                    let bx = self.cur_x + c as i32;
                    let by = self.cur_y + r as i32;
                    if by >= 0 && by < BOARD_H as i32 && bx >= 0 && bx < BOARD_W as i32 {
                        let sx = ORIGIN_X + (bx * 2) as u8;
                        let sy = ORIGIN_Y + 1 + by as u8;
                        Self::draw_cell(sx, sy, color, b'#');
                        Self::draw_cell(sx + 1, sy, color, b'#');
                    }
                }
            }
        }

        // Draw HUD / Scoreboard on side
        set_cursor(ORIGIN_X + (BOARD_W * 2) as u8 + 4, ORIGIN_Y + 2);
        print("=== CYKIS ===");
        set_cursor(ORIGIN_X + (BOARD_W * 2) as u8 + 4, ORIGIN_Y + 4);
        print("Score: ");
        print_num(self.score);
        set_cursor(ORIGIN_X + (BOARD_W * 2) as u8 + 4, ORIGIN_Y + 6);
        print("Lines: ");
        print_num(self.lines);
        set_cursor(ORIGIN_X + (BOARD_W * 2) as u8 + 4, ORIGIN_Y + 9);
        print("Controls:");
        set_cursor(ORIGIN_X + (BOARD_W * 2) as u8 + 4, ORIGIN_Y + 11);
        print("Left/Right: Move");
        set_cursor(ORIGIN_X + (BOARD_W * 2) as u8 + 4, ORIGIN_Y + 12);
        print("Up:         Rotate");
        set_cursor(ORIGIN_X + (BOARD_W * 2) as u8 + 4, ORIGIN_Y + 13);
        print("Down:       Soft Drop");
        set_cursor(ORIGIN_X + (BOARD_W * 2) as u8 + 4, ORIGIN_Y + 14);
        print("Space:      Hard Drop");
        set_cursor(ORIGIN_X + (BOARD_W * 2) as u8 + 4, ORIGIN_Y + 15);
        print("Ctrl+C / Q: Quit");
    }
}

fn print_num(mut n: u32) {
    if n == 0 {
        putc(b'0');
        return;
    }
    let mut buf = [0u8; 12];
    let mut i = 0;
    while n > 0 {
        buf[i] = b'0' + (n % 10) as u8;
        n /= 10;
        i += 1;
    }
    while i > 0 {
        i -= 1;
        putc(buf[i]);
    }
}

#[no_mangle]
#[link_section = ".entry"]
pub extern "C" fn main(_argc: isize, _argv: *const *const u8) -> i32 {
    clear();
    let mut game = GameState::new();
    game.render();

    // Gravity loop settings:
    // Drop automatically every DROP_TICKS cycles of the non-blocking input poll
    const FRAME_MS: u32 = 25; // 25ms per frame
    const DROP_INTERVAL: u32 = 16; // 16 * 25ms = 400ms between automatic drops
    let mut tick_counter: u32 = 0;

    while !game.game_over {
        // Accurate hardware PIT delay (25ms per frame)
        msleep(FRAME_MS);
        tick_counter += 1;

        // 1. Process non-blocking keyboard input
        if key_available() {
            let key = read_char();
            let mut need_redraw = false;

            match key {
                0x03 | b'q' | b'Q' => {
                    // Quit game cleanly
                    break;
                }
                0x82 | b'a' | b'A' => { // Left arrow or 'a'
                    if !game.check_collision(game.cur_piece, game.cur_rot, game.cur_x - 1, game.cur_y) {
                        game.cur_x -= 1;
                        need_redraw = true;
                    }
                }
                0x83 | b'd' | b'D' => { // Right arrow or 'd'
                    if !game.check_collision(game.cur_piece, game.cur_rot, game.cur_x + 1, game.cur_y) {
                        game.cur_x += 1;
                        need_redraw = true;
                    }
                }
                0x80 | b'w' | b'W' => { // Up arrow or 'w' (Rotate)
                    let next_rot = (game.cur_rot + 1) % 4;
                    if !game.check_collision(game.cur_piece, next_rot, game.cur_x, game.cur_y) {
                        game.cur_rot = next_rot;
                        need_redraw = true;
                    }
                }
                0x81 | b's' | b'S' => { // Down arrow or 's' (Soft drop)
                    if !game.check_collision(game.cur_piece, game.cur_rot, game.cur_x, game.cur_y + 1) {
                        game.cur_y += 1;
                        need_redraw = true;
                    } else {
                        game.lock_piece();
                        need_redraw = true;
                    }
                    tick_counter = 0; // Reset drop timer on manual drop
                }
                b' ' => { // Hard drop
                    while !game.check_collision(game.cur_piece, game.cur_rot, game.cur_x, game.cur_y + 1) {
                        game.cur_y += 1;
                    }
                    game.lock_piece();
                    need_redraw = true;
                    tick_counter = 0;
                }
                _ => {}
            }

            if need_redraw {
                game.render();
            }
        }

        // 2. Automatic gravity: drop block after short period of time
        if tick_counter >= DROP_INTERVAL {
            tick_counter = 0;
            if !game.check_collision(game.cur_piece, game.cur_rot, game.cur_x, game.cur_y + 1) {
                game.cur_y += 1;
            } else {
                game.lock_piece();
            }
            game.render();
        }
    }

    // Game Over display
    set_cursor(ORIGIN_X + 2, ORIGIN_Y + (BOARD_H / 2) as u8);
    print("*** GAME OVER ***");
    sound_tone(150, 400);
    sleep(2);

    clear();
    set_cursor(0, 0);
    0
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {
        unsafe { core::arch::asm!("hlt"); }
    }
}
