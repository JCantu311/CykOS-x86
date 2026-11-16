//! CYKIS: a compact Tetris-style game for the CykOS graphics API.

#![no_std]
#![no_main]

use core::panic::PanicInfo;

mod cykos;
use cykos::*;

const BOARD_W: usize = 10;
const BOARD_H: usize = 20;
const FRAME_MS: u32 = 20;
const LOCK_DELAY_MS: u32 = 500;
const MAX_LOCK_RESETS: u8 = 15;
const BOARD_X: i16 = 38;
const BOARD_Y: i16 = 24;
const CELL_W: i16 = 26;
const CELL_H: i16 = 21;
const GRID_COLOR: u8 = 8;

// Four rotations of each of the seven standard tetrominoes in 4x4 masks.
static PIECES: [[u16; 4]; 7] = [
    [0x0F00, 0x2222, 0x00F0, 0x4444],
    [0x8E00, 0x6440, 0x0E20, 0x44C0],
    [0x2E00, 0x4460, 0x0E80, 0xC440],
    [0x6600, 0x6600, 0x6600, 0x6600],
    [0x6C00, 0x4620, 0x06C0, 0x8C40],
    [0x4E00, 0x4640, 0x0E40, 0x4C40],
    [0xC600, 0x2640, 0x0C60, 0x4C80],
];

// Standard VGA palette indices: cyan, blue, brown, yellow, green, magenta, red.
static PIECE_COLORS: [u8; 7] = [11, 1, 6, 14, 2, 13, 4];

struct PieceBag {
    pieces: [u8; 7],
    cursor: usize,
    state: u32,
}

impl PieceBag {
    fn new() -> Self {
        let low: u32;
        let high: u32;
        unsafe {
            core::arch::asm!("rdtsc", out("eax") low, out("edx") high,
                             options(nomem, nostack, preserves_flags));
        }
        let seed = low ^ high.rotate_left(13) ^ 0x9E3779B9;
        let mut bag = Self { pieces: [0; 7], cursor: 7,
                             state: if seed == 0 { 0xA341316C } else { seed } };
        bag.shuffle();
        bag
    }

    fn random(&mut self) -> u32 {
        // xorshift32; state is kept nonzero by the seed fallback above.
        let mut x = self.state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        self.state = x;
        x
    }

    fn below(&mut self, bound: u32) -> u32 {
        let threshold = bound.wrapping_neg() % bound;
        loop {
            let value = self.random();
            if value >= threshold { return value % bound; }
        }
    }

    fn shuffle(&mut self) {
        for i in 0..7 { self.pieces[i] = i as u8; }
        for i in (1..7).rev() {
            let j = self.below(i as u32 + 1) as usize;
            self.pieces.swap(i, j);
        }
        self.cursor = 0;
    }

    fn next(&mut self) -> usize {
        if self.cursor >= self.pieces.len() { self.shuffle(); }
        let piece = self.pieces[self.cursor] as usize;
        self.cursor += 1;
        piece
    }
}

fn piece_has_cell(piece: usize, rot: usize, row: usize, col: usize) -> bool {
    (PIECES[piece][rot & 3] & (1 << (15 - (row * 4 + col)))) != 0
}

struct GameState {
    board: [[u8; BOARD_W]; BOARD_H],
    bag: PieceBag,
    cur_piece: usize,
    next_piece: usize,
    cur_rot: usize,
    cur_x: i32,
    cur_y: i32,
    score: u32,
    lines: u32,
    board_revision: u32,
    game_over: bool,
}

impl GameState {
    fn new() -> Self {
        let mut bag = PieceBag::new();
        let cur_piece = bag.next();
        let next_piece = bag.next();
        let mut game = Self {
            board: [[0; BOARD_W]; BOARD_H],
            bag,
            cur_piece,
            next_piece,
            cur_rot: 0,
            cur_x: 3,
            cur_y: 0,
            score: 0,
            lines: 0,
            board_revision: 0,
            game_over: false,
        };
        if game.check_collision(game.cur_piece, 0, game.cur_x, game.cur_y) {
            game.game_over = true;
        }
        game
    }

    fn check_collision(&self, piece: usize, rot: usize, px: i32, py: i32) -> bool {
        for row in 0..4 {
            for col in 0..4 {
                if !piece_has_cell(piece, rot, row, col) { continue; }
                let bx = px + col as i32;
                let by = py + row as i32;
                if bx < 0 || bx >= BOARD_W as i32 || by >= BOARD_H as i32 { return true; }
                if by >= 0 && self.board[by as usize][bx as usize] != 0 { return true; }
            }
        }
        false
    }

    fn grounded(&self) -> bool {
        self.check_collision(self.cur_piece, self.cur_rot, self.cur_x, self.cur_y + 1)
    }

    fn try_move(&mut self, dx: i32, dy: i32) -> bool {
        if self.check_collision(self.cur_piece, self.cur_rot, self.cur_x + dx, self.cur_y + dy) {
            return false;
        }
        self.cur_x += dx;
        self.cur_y += dy;
        true
    }

    fn try_rotate(&mut self) -> bool {
        let next_rot = (self.cur_rot + 1) & 3;
        // Small wall kicks make rotation near a wall feel like modern Tetris.
        let kicks = [0, -1, 1, -2, 2];
        for kick in kicks {
            if !self.check_collision(self.cur_piece, next_rot, self.cur_x + kick, self.cur_y) {
                self.cur_x += kick;
                self.cur_rot = next_rot;
                return true;
            }
        }
        false
    }

    fn clear_lines(&mut self) -> u32 {
        let mut cleared = 0;
        let mut row = (BOARD_H as i32) - 1;
        while row >= 0 {
            let mut full = true;
            for col in 0..BOARD_W {
                if self.board[row as usize][col] == 0 { full = false; break; }
            }
            if full {
                cleared += 1;
                for move_row in (1..=row as usize).rev() {
                    self.board[move_row] = self.board[move_row - 1];
                }
                self.board[0] = [0; BOARD_W];
            } else {
                row -= 1;
            }
        }
        cleared
    }

    fn lock_piece(&mut self) -> u32 {
        self.board_revision = self.board_revision.wrapping_add(1);
        for row in 0..4 {
            for col in 0..4 {
                if !piece_has_cell(self.cur_piece, self.cur_rot, row, col) { continue; }
                let bx = self.cur_x + col as i32;
                let by = self.cur_y + row as i32;
                if by >= 0 && by < BOARD_H as i32 && bx >= 0 && bx < BOARD_W as i32 {
                    self.board[by as usize][bx as usize] = PIECE_COLORS[self.cur_piece];
                }
            }
        }

        let cleared = self.clear_lines();
        if cleared > 0 {
            let line_score = match cleared { 1 => 40, 2 => 100, 3 => 300, _ => 1200 };
            self.score = self.score.saturating_add(line_score * (self.lines / 10 + 1));
            self.lines += cleared;
            sound_chime();
        }

        self.cur_piece = self.next_piece;
        self.next_piece = self.bag.next();
        self.cur_rot = 0;
        self.cur_x = 3;
        self.cur_y = 0;
        if self.check_collision(self.cur_piece, self.cur_rot, self.cur_x, self.cur_y) {
            self.game_over = true;
        }
        cleared
    }

    fn level(&self) -> u32 { self.lines / 10 }

    fn gravity_period_ms(&self) -> u32 {
        const PERIODS: [u32; 15] = [1000, 850, 700, 600, 500, 420, 340, 270, 210, 160, 120, 90, 70, 55, 45];
        PERIODS[(self.level().min(14)) as usize]
    }
}

fn draw_block(x: i16, y: i16, color: u8, width: i16, height: i16) {
    if width <= 0 || height <= 0 || width > 26 || height > 21 { return; }
    let mut sprite = [255u8; 26 * 21];
    let w = width as usize;
    let h = height as usize;
    for py in 1..h.saturating_sub(1) {
        for px in 1..w.saturating_sub(1) {
            sprite[py * w + px] = color;
        }
    }
    // Beveled, indexed-color bitmap tile. 255 is the graphics API's transparent index.
    for py in 2..h.saturating_sub(3) {
        for px in 2..w.saturating_sub(3) {
            if py < 4 || px < 4 { sprite[py * w + px] = 15; }
        }
    }
    for py in 3..h.saturating_sub(3) {
        for px in w.saturating_sub(4)..w.saturating_sub(2) {
            sprite[py * w + px] = 8;
        }
    }
    let _ = gfx_draw_bitmap(x, y, w as u32, h as u32, w as u32,
                            GFX_BITMAP_INDEX8, &sprite[..w * h]);
}

fn draw_piece(piece: usize, rot: usize, px: i32, py: i32, color: u8, ghost: bool) {
    for row in 0..4 {
        for col in 0..4 {
            if !piece_has_cell(piece, rot, row, col) { continue; }
            let board_x = px + col as i32;
            let board_y = py + row as i32;
            if board_x < 0 || board_x >= BOARD_W as i32 || board_y < 0 || board_y >= BOARD_H as i32 { continue; }
            let x = BOARD_X + board_x as i16 * CELL_W;
            let y = BOARD_Y + board_y as i16 * CELL_H;
            if ghost {
                gfx_fill_rect(x + 3, y + 3, CELL_W - 6, 2, color);
                gfx_fill_rect(x + 3, y + CELL_H - 5, CELL_W - 6, 2, color);
                gfx_fill_rect(x + 3, y + 3, 2, CELL_H - 6, color);
                gfx_fill_rect(x + CELL_W - 5, y + 3, 2, CELL_H - 6, color);
            } else {
                draw_block(x, y, color, CELL_W, CELL_H);
            }
        }
    }
}

fn font_rows(ch: u8) -> [u8; 5] {
    match ch {
        b'A' => [2, 5, 7, 5, 5], b'B' => [6, 5, 6, 5, 6], b'C' => [3, 4, 4, 4, 3],
        b'D' => [6, 5, 5, 5, 6], b'E' => [7, 4, 6, 4, 7], b'F' => [7, 4, 6, 4, 4],
        b'G' => [3, 4, 5, 5, 3], b'H' => [5, 5, 7, 5, 5], b'I' => [7, 2, 2, 2, 7],
        b'J' => [1, 1, 1, 5, 2], b'K' => [5, 5, 6, 5, 5], b'L' => [4, 4, 4, 4, 7],
        b'M' => [5, 7, 7, 5, 5], b'N' => [5, 7, 7, 7, 5], b'O' => [2, 5, 5, 5, 2],
        b'P' => [6, 5, 6, 4, 4], b'Q' => [2, 5, 5, 7, 3], b'R' => [6, 5, 6, 5, 5],
        b'S' => [3, 4, 2, 1, 6], b'T' => [7, 2, 2, 2, 2], b'U' => [5, 5, 5, 5, 7],
        b'V' => [5, 5, 5, 5, 2], b'W' => [5, 5, 7, 7, 5], b'X' => [5, 5, 2, 5, 5],
        b'Y' => [5, 5, 2, 2, 2], b'Z' => [7, 1, 2, 4, 7],
        b'0' => [7, 5, 5, 5, 7], b'1' => [2, 6, 2, 2, 7], b'2' => [6, 1, 2, 4, 7],
        b'3' => [6, 1, 2, 1, 6], b'4' => [5, 5, 7, 1, 1], b'5' => [7, 4, 6, 1, 6],
        b'6' => [3, 4, 6, 5, 2], b'7' => [7, 1, 2, 2, 2], b'8' => [2, 5, 2, 5, 2],
        b'9' => [2, 5, 3, 1, 6], b'/' => [1, 1, 2, 4, 4], b':' => [0, 2, 0, 2, 0],
        _ => [0; 5],
    }
}

fn draw_text(text: &[u8], x: i16, y: i16, color: u8, scale: i16) {
    const MAX_WIDTH: usize = 160;
    const MAX_HEIGHT: usize = 20;
    if scale <= 0 { return; }
    let scale = scale as usize;
    let width = text.len() * 4 * scale;
    let height = 5 * scale;
    if width == 0 || width > MAX_WIDTH || height > MAX_HEIGHT { return; }

    let mut bitmap = [255u8; MAX_WIDTH * MAX_HEIGHT];
    let pixel = color & 0x0F;
    for (character, &ch) in text.iter().enumerate() {
        let rows = font_rows(ch.to_ascii_uppercase());
        for row in 0..5 {
            for col in 0..3 {
                if rows[row] & (1 << (2 - col)) == 0 { continue; }
                for dy in 0..scale {
                    for dx in 0..scale {
                        let px = character * 4 * scale + col * scale + dx;
                        let py = row * scale + dy;
                        bitmap[py * width + px] = pixel;
                    }
                }
            }
        }
    }
    let _ = gfx_draw_bitmap(x, y, width as u32, height as u32, width as u32,
                            GFX_BITMAP_INDEX8, &bitmap[..width * height]);
}

fn draw_digit(x: i16, y: i16, value: u8, color: u8, scale: i16) {
    let segments: [u8; 10] = [0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F];
    let mask = segments[(value % 10) as usize];
    let t = scale;
    let w = 4 * scale;
    let h = 7 * scale;
    if mask & 0x01 != 0 { gfx_fill_rect(x + t, y, w, t, color); }
    if mask & 0x02 != 0 { gfx_fill_rect(x + w + t, y + t, t, 2 * scale, color); }
    if mask & 0x04 != 0 { gfx_fill_rect(x + w + t, y + 4 * scale, t, 2 * scale, color); }
    if mask & 0x08 != 0 { gfx_fill_rect(x + t, y + h, w, t, color); }
    if mask & 0x10 != 0 { gfx_fill_rect(x, y + 4 * scale, t, 2 * scale, color); }
    if mask & 0x20 != 0 { gfx_fill_rect(x, y + t, t, 2 * scale, color); }
    if mask & 0x40 != 0 { gfx_fill_rect(x + t, y + 3 * scale, w, t, color); }
}

fn draw_number(mut value: u32, x: i16, y: i16, color: u8, scale: i16) {
    let mut digits = [0u8; 10];
    let mut count = 0;
    if value == 0 { digits[0] = 0; count = 1; }
    while value > 0 && count < digits.len() {
        digits[count] = (value % 10) as u8;
        value /= 10;
        count += 1;
    }
    let digit_width = 6 * scale;
    for i in 0..count {
        draw_digit(x + (count - i - 1) as i16 * digit_width, y, digits[i], color, scale);
    }
}

fn draw_preview(piece: usize, x: i16, y: i16) {
    for row in 0..4 {
        for col in 0..4 {
            if piece_has_cell(piece, 0, row, col) {
                draw_block(x + col as i16 * 18, y + row as i16 * 18,
                           PIECE_COLORS[piece], 18, 18);
            }
        }
    }
}

impl GameState {
    fn draw_board(&self) {
        gfx_fill_rect(BOARD_X - 4, BOARD_Y - 4, BOARD_W as i16 * CELL_W + 8,
                      BOARD_H as i16 * CELL_H + 8, GRID_COLOR);
        gfx_fill_rect(BOARD_X, BOARD_Y, BOARD_W as i16 * CELL_W,
                      BOARD_H as i16 * CELL_H, 2);

        for row in 0..BOARD_H {
            for col in 0..BOARD_W {
                if self.board[row][col] != 0 {
                    draw_block(BOARD_X + col as i16 * CELL_W,
                               BOARD_Y + row as i16 * CELL_H,
                               self.board[row][col], CELL_W, CELL_H);
                }
            }
        }
    }

    fn restore_cell(&self, x: i32, y: i32) {
        if x < 0 || x >= BOARD_W as i32 || y < 0 || y >= BOARD_H as i32 { return; }
        let px = BOARD_X + x as i16 * CELL_W;
        let py = BOARD_Y + y as i16 * CELL_H;
        gfx_fill_rect(px + 1, py + 1, CELL_W - 2, CELL_H - 2, 2);
        let color = self.board[y as usize][x as usize];
        if color != 0 { draw_block(px, py, color, CELL_W, CELL_H); }
    }

    fn erase_piece_at(&self, piece: usize, rot: usize, px: i32, py: i32) {
        for row in 0..4 {
            for col in 0..4 {
                if piece_has_cell(piece, rot, row, col) {
                    self.restore_cell(px + col as i32, py + row as i32);
                }
            }
        }
    }

    fn render(&self, ghost_y: i32, previous: Option<(usize, usize, i32, i32, i32)>,
              redraw_board: bool, redraw_stats: bool, redraw_next: bool) {
        if redraw_board {
            self.draw_board();
        } else if let Some((piece, rot, x, y, old_ghost_y)) = previous {
            self.erase_piece_at(piece, rot, x, y);
            if old_ghost_y != y {
                self.erase_piece_at(piece, rot, x, old_ghost_y);
            }
        }

        if ghost_y != self.cur_y {
            draw_piece(self.cur_piece, self.cur_rot, self.cur_x, ghost_y, GRID_COLOR, true);
        }
        draw_piece(self.cur_piece, self.cur_rot, self.cur_x, self.cur_y,
                   PIECE_COLORS[self.cur_piece], false);

        let panel_x = 340;
        draw_text(b"CYKIS", panel_x, 34, 252, 3);
        draw_text(b"SCORE", panel_x, 104, 255, 3);
        draw_text(b"LINES", panel_x, 178, 255, 3);
        draw_text(b"LEVEL", panel_x, 252, 255, 3);
        if redraw_stats {
            gfx_fill_rect(panel_x, 122, 256, 40, 0);
            draw_number(self.score, panel_x, 124, 15, 4);
            gfx_fill_rect(panel_x, 196, 256, 40, 0);
            draw_number(self.lines, panel_x, 198, 14, 4);
            gfx_fill_rect(panel_x, 270, 256, 40, 0);
            draw_number(self.level(), panel_x, 272, 11, 4);
        }
        draw_text(b"NEXT", panel_x, 326, 255, 3);
        if redraw_next {
            gfx_fill_rect(panel_x + 2, 344, 92, 78, GRID_COLOR);
            gfx_fill_rect(panel_x + 4, 346, 88, 74, 0);
            draw_preview(self.next_piece, panel_x + 12, 346);
        }
        draw_text(b"A D MOVE", panel_x, 426, 15, 2);
        draw_text(b"W ROTATE", panel_x, 439, 15, 2);
        draw_text(b"S SOFT", panel_x, 452, 15, 2);
        draw_text(b"SPACE DROP", panel_x, 465, 15, 2);
    }
}

fn current_ghost_y(game: &GameState) -> i32 {
    let mut y = game.cur_y;
    while !game.check_collision(game.cur_piece, game.cur_rot, game.cur_x, y + 1) { y += 1; }
    y
}

fn drop_piece(game: &mut GameState, hard: bool) -> bool {
    if hard {
        let start_y = game.cur_y;
        while game.try_move(0, 1) {}
        game.score = game.score.saturating_add(((game.cur_y - start_y) as u32) * 2);
        game.lock_piece();
        return true;
    }
    if game.try_move(0, 1) {
        game.score = game.score.saturating_add(1);
        true
    } else {
        false
    }
}

#[no_mangle]
#[link_section = ".entry"]
pub extern "C" fn main(_argc: isize, _argv: *const *const u8) -> i32 {
    if gfx_set_mode(GFX_MODE_640X480) != 0 {
        gfx_set_mode(GFX_MODE_TEXT);
        println("CYKIS: VGA 640x480 mode unavailable.");
        return -1;
    }

    gfx_clear(0);
    let mut game = GameState::new();
    let mut gravity_elapsed = 0u32;
    let mut lock_elapsed = 0u32;
    let mut lock_resets = 0u8;
    let mut displayed_score = game.score;
    let mut displayed_lines = game.lines;
    let mut displayed_level = game.level();
    let mut displayed_next = game.next_piece;
    let mut displayed_board_revision = game.board_revision;
    game.render(current_ghost_y(&game), None, true, true, true);

    while !game.game_over {
        let previous_piece = (game.cur_piece, game.cur_rot, game.cur_x, game.cur_y,
                              current_ghost_y(&game));
        msleep(FRAME_MS);
        gravity_elapsed = gravity_elapsed.saturating_add(FRAME_MS);
        let mut changed = false;
        let grounded_before_input = game.grounded();

        if let Some(key) = try_read_char() {
            match key {
                0x03 | b'q' | b'Q' => break,
                0x82 | b'a' | b'A' => changed = game.try_move(-1, 0),
                0x83 | b'd' | b'D' => changed = game.try_move(1, 0),
                0x80 | b'w' | b'W' => changed = game.try_rotate(),
                0x81 | b's' | b'S' => {
                    changed = drop_piece(&mut game, false);
                    if changed { gravity_elapsed = 0; }
                }
                b' ' => {
                    drop_piece(&mut game, true);
                    gravity_elapsed = 0;
                    lock_elapsed = 0;
                    lock_resets = 0;
                    changed = true;
                }
                _ => {}
            }
        }

        if changed && grounded_before_input && lock_resets < MAX_LOCK_RESETS {
            lock_elapsed = 0;
            lock_resets += 1;
        }

        if !game.game_over && gravity_elapsed >= game.gravity_period_ms() {
            gravity_elapsed -= game.gravity_period_ms();
            if game.try_move(0, 1) { changed = true; }
            else { changed = true; }
        }

        if game.grounded() {
            lock_elapsed = lock_elapsed.saturating_add(FRAME_MS);
            if lock_elapsed >= LOCK_DELAY_MS {
                game.lock_piece();
                lock_elapsed = 0;
                lock_resets = 0;
                gravity_elapsed = 0;
                changed = true;
            }
        } else {
            lock_elapsed = 0;
        }

        if changed && !game.game_over {
            let score = game.score;
            let lines = game.lines;
            let level = game.level();
            let next = game.next_piece;
            let redraw_board = game.board_revision != displayed_board_revision;
            let redraw_stats = score != displayed_score || lines != displayed_lines ||
                               level != displayed_level;
            let redraw_next = next != displayed_next;
            game.render(current_ghost_y(&game), Some(previous_piece), redraw_board,
                        redraw_stats, redraw_next);
            displayed_score = score;
            displayed_lines = lines;
            displayed_level = level;
            displayed_next = next;
            displayed_board_revision = game.board_revision;
        }
    }

    if game.game_over {
        gfx_fill_rect(160, 190, 320, 100, 4);
        gfx_fill_rect(164, 194, 312, 92, 0);
        draw_text(b"GAME OVER", 206, 216, 15, 4);
        draw_text(b"PRESS ANY KEY", 216, 258, 15, 2);
        sound_tone(150, 250);
        // Drain the key that caused the game over, leave the prompt visible,
        // then use the blocking character API used by the shell. The nonblocking
        // game poll can miss input here when its IRQ queue is empty.
        while try_read_char().is_some() {}
        msleep(1500);
        let _ = read_char();
    }
    gfx_set_mode(GFX_MODE_TEXT);
    0
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop { unsafe { core::arch::asm!("hlt"); } }
}
