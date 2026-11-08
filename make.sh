nasm -f elf32 boot.asm -o boot.o
cargo build --target i686-unknown-baremetal.json --release
ld -m elf_i386 -T linker.ld boot.o target/i686-unknown-baremetal/release/lib<your_crate_name>.a -o os_image.bin
