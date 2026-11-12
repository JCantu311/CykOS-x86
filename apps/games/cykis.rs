#![no_std]
#![no_main]

use core::panic::PanicInfo;
use core::ptr;

const WIDTH: usize = 10;
const HEIGHT: usize = 20;
const SCREEN_WIDTH: usize = 80;
const SCREEN_HEIGHT: usize = 25;
const SCREEN_BYTES: usize = SCREEN_WIDTH * SCREEN_HEIGHT * 2;

const SHAPES: [[[u8; 2]; 4]; 7] = [
    [[0, 1], [1, 1], [2, 1], [3, 1]],
    [[1, 1], [2, 1], [1, 2], [2, 2]],
    [[1, 1], [0, 2], [1, 2], [2, 2]],
    [[1, 1], [2, 1], [0, 2], [1, 2]],
    [[0, 1], [1, 1], [1, 2], [2, 2]],
    [[0, 1], [0, 2], [1, 2], [2, 2]],
    [[2, 1], [0, 2], [1, 2], [2, 2]],
];

const COLORS: [u8; 7] = [0x0B, 0x0E, 0x0D, 0x0A, 0x0C, 0x09, 0x06];

extern "C" {
    fn get_char() -> u8;
    fn has_key() -> i32;
    fn set_cursor_pos(col: i32, row: i32);
}

fn cell(screen: &mut [u8; SCREEN_BYTES], x: usize, y: usize, ch: u8, color: u8) {
    if x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT { return; }
    let offset = (y * SCREEN_WIDTH + x) * 2;
    screen[offset] = ch;
    screen[offset + 1] = color;
}

fn text(screen: &mut [u8; SCREEN_BYTES], x: usize, y: usize, value: &[u8], color: u8) {
    for (index, ch) in value.iter().enumerate() {
        if *ch == 0 { break; }
        cell(screen, x + index, y, *ch, color);
    }
}

fn number(screen: &mut [u8; SCREEN_BYTES], x: usize, y: usize, mut value: u32) {
    let mut digits = [0u8; 10];
    let mut count = 0;
    loop {
        unsafe { *digits.get_unchecked_mut(count) = b'0' + (value % 10) as u8; }
        count += 1;
        value /= 10;
        if value == 0 || count == digits.len() { break; }
    }
    for index in 0..count {
        cell(screen, x + index, y, digits[count - index - 1], 0x0E);
    }
}

fn occupied(piece: usize, rotation: u8, x: i32, y: i32) -> bool {
    let shape = unsafe { SHAPES.get_unchecked(piece) };
    for point in shape.iter() {
        let mut px = point[0] as i32;
        let mut py = point[1] as i32;
        for _ in 0..rotation {
            let next_x = 3 - py;
            py = px;
            px = next_x;
        }
        if px == x && py == y { return true; }
    }
    false
}

fn collides(board: &[[u8; WIDTH]; HEIGHT], piece: usize, rotation: u8, x: i32, y: i32) -> bool {
    for cy in 0..4 {
        for cx in 0..4 {
            if !occupied(piece, rotation, cx, cy) { continue; }
            let board_x = x + cx;
            let board_y = y + cy;
            if board_x < 0 || board_x >= WIDTH as i32 || board_y >= HEIGHT as i32 { return true; }
            if board_y >= 0 && unsafe { *board.get_unchecked(board_y as usize).get_unchecked(board_x as usize) != 0 } { return true; }
        }
    }
    false
}

fn lock(board: &mut [[u8; WIDTH]; HEIGHT], piece: usize, rotation: u8, x: i32, y: i32) {
    for cy in 0..4 {
        for cx in 0..4 {
            if !occupied(piece, rotation, cx, cy) { continue; }
            let board_x = x + cx;
            let board_y = y + cy;
            if board_x >= 0 && board_x < WIDTH as i32 && board_y >= 0 && board_y < HEIGHT as i32 {
                unsafe { *board.get_unchecked_mut(board_y as usize).get_unchecked_mut(board_x as usize) = (piece + 1) as u8; }
            }
        }
    }
}

fn clear_lines(board: &mut [[u8; WIDTH]; HEIGHT]) -> u32 {
    let mut cleared = 0;
    let mut row = HEIGHT as i32 - 1;
    while row >= 0 {
        if unsafe { board.get_unchecked(row as usize).iter().all(|cell| *cell != 0) } {
            for move_row in (1..=row as usize).rev() {
                unsafe { *board.get_unchecked_mut(move_row) = *board.get_unchecked(move_row - 1); }
            }
            board[0] = [0; WIDTH];
            cleared += 1;
        } else {
            row -= 1;
        }
    }
    cleared
}

fn render(board: &[[u8; WIDTH]; HEIGHT], screen: &mut [u8; SCREEN_BYTES], piece: usize, rotation: u8, px: i32, py: i32, score: u32, lines: u32, level: u32, paused: bool, over: bool) {
    for pair in screen.chunks_exact_mut(2) {
        pair[0] = b' ';
        pair[1] = 0x07;
    }
    text(screen, 3, 0, b"CYKIS / FALLING BLOCKS", 0x0B);
    text(screen, 3, 1, b"================================", 0x08);

    let left = 2;
    let top = 2;
    for x in 0..(WIDTH * 2 + 2) {
        cell(screen, left + x, top, b'=', 0x08);
        cell(screen, left + x, top + HEIGHT + 1, b'=', 0x08);
    }
    for y in 0..HEIGHT {
        cell(screen, left, top + y + 1, b'|', 0x08);
        cell(screen, left + WIDTH * 2 + 1, top + y + 1, b'|', 0x08);
        for x in 0..WIDTH {
            let mut value = unsafe { *board.get_unchecked(y).get_unchecked(x) };
            if value == 0 && occupied(piece, rotation, x as i32 - px, y as i32 - py) {
                value = (piece + 1) as u8;
            }
            let color = if value == 0 { 0x08 } else { unsafe { *COLORS.get_unchecked((value - 1) as usize) } };
            let glyph = if value == 0 { b'.' } else { b'#' };
            cell(screen, left + 1 + x * 2, top + y + 1, glyph, color);
            cell(screen, left + 2 + x * 2, top + y + 1, glyph, color);
        }
    }

    let panel = 29;
    text(screen, panel, 3, b"SCORE", 0x0F);
    number(screen, panel, 4, score);
    text(screen, panel, 6, b"LINES", 0x0F);
    number(screen, panel, 7, lines);
    text(screen, panel, 9, b"LEVEL", 0x0F);
    number(screen, panel, 10, level);
    text(screen, panel, 13, b"ARROWS  MOVE", 0x0A);
    text(screen, panel, 14, b"UP      TURN", 0x0A);
    text(screen, panel, 15, b"SPACE   DROP", 0x0A);
    text(screen, panel, 16, b"P       PAUSE", 0x0A);
    text(screen, panel, 17, b"Q       QUIT", 0x0A);
    if paused { text(screen, panel, 20, b"PAUSED", 0x0E); }
    if over { text(screen, panel, 20, b"STACKED OUT", 0x0C); }

    unsafe {
        ptr::copy_nonoverlapping(screen.as_ptr(), 0xB8000 as *mut u8, SCREEN_BYTES);
        set_cursor_pos(0, (SCREEN_HEIGHT - 1) as i32);
    }
}

#[no_mangle]
#[link_section = ".entry"]
pub extern "C" fn main(_argc: isize, _argv: *const *const u8) -> i32 {
    let mut board = [[0u8; WIDTH]; HEIGHT];
    let mut screen = [0u8; SCREEN_BYTES];
    let mut piece = 0usize;
    let mut rotation = 0u8;
    let mut x = 3i32;
    let mut y = -1i32;
    let mut next = 1usize;
    let mut score = 0u32;
    let mut lines = 0u32;
    let mut level = 1u32;
    let mut frame = 0u32;
    let mut paused = false;
    let mut over = false;

    loop {
        while unsafe { has_key() } != 0 {
            let key = unsafe { get_char() };
            if key == b'q' || key == b'Q' { return 0; }
            if key == b'p' || key == b'P' { paused = !paused; continue; }
            if over { continue; }
            if paused { continue; }
            match key {
                0x82 if !collides(&board, piece, rotation, x - 1, y) => x -= 1,
                0x83 if !collides(&board, piece, rotation, x + 1, y) => x += 1,
                0x80 => {
                    let turned = (rotation + 1) & 3;
                    if !collides(&board, piece, turned, x, y) { rotation = turned; }
                }
                0x81 if !collides(&board, piece, rotation, x, y + 1) => { y += 1; score += 1; }
                b' ' => { while !collides(&board, piece, rotation, x, y + 1) { y += 1; score += 2; } frame = 20; }
                _ => {}
            }
        }

        if !paused && !over {
            frame += 1;
            let gravity = if level > 12 { 4 } else { 22u32.saturating_sub(level) };
            if frame >= gravity {
                frame = 0;
                if !collides(&board, piece, rotation, x, y + 1) {
                    y += 1;
                } else {
                    lock(&mut board, piece, rotation, x, y);
                    if y < 0 {
                        over = true;
                    } else {
                        let removed = clear_lines(&mut board);
                        lines += removed;
                        score += match removed { 1 => 100, 2 => 300, 3 => 500, 4 => 800, _ => 0 } * level;
                        level = lines / 10 + 1;
                        piece = next;
                        next = (next + 1 + (score as usize % 5)) % SHAPES.len();
                        rotation = 0;
                        x = 3;
                        y = -1;
                        if collides(&board, piece, rotation, x, y) { over = true; }
                    }
                }
            }
        }

        render(&board, &mut screen, piece, rotation, x, y, score, lines, level, paused, over);
        for _ in 0..120_000 { core::hint::spin_loop(); }
    }
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop { unsafe { core::arch::asm!("hlt"); } }
}
