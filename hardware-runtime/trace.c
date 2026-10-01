// Runs before ARM9 BSS/TLS/constructors. No libc or libnds dependencies.
// Displays ARM7 stage (top) and IPC status (bottom), as two hex digits each.
typedef unsigned short u16;
typedef unsigned int u32;
static const unsigned char glyphs[16][7] = {
    {6,9,9,9,9,9,6}, {2,6,2,2,2,2,7}, {6,9,1,2,4,8,15},
    {14,1,1,6,1,1,14}, {9,9,9,15,1,1,1}, {15,8,8,14,1,1,14},
    {6,8,8,14,9,9,6}, {15,1,2,2,4,4,4}, {6,9,9,6,9,9,6},
    {6,9,9,7,1,1,6}, {6,9,9,15,9,9,9}, {14,9,9,14,9,9,14},
    {7,8,8,8,8,8,7}, {14,9,9,9,9,9,14}, {15,8,8,14,8,8,15},
    {15,8,8,14,8,8,8}
};
static void digit(volatile u16 *fb, unsigned x, unsigned y, unsigned n, u16 color) {
    for (unsigned row=0; row<7; row++)
        for (unsigned col=0; col<4; col++)
            for (unsigned dy=0; dy<4; dy++)
                for (unsigned dx=0; dx<4; dx++)
                    fb[(y+row*4+dy)*256+x+col*4+dx] =
                        (glyphs[n & 15][row] & (8>>col)) ? color : 0x8000;
}
void early_boot_trace(void) {
    *(volatile u16 *)0x04000304 = 0x8203;
    *(volatile unsigned char *)0x04000240 = 0x80;
    *(volatile u32 *)0x04000000 = 0x00020000;
    *(volatile u16 *)0x0400006c = 0;
    volatile u16 *fb = (volatile u16 *)0x06800000;
    unsigned stage = *(volatile u32 *)0x0c37f000;
    unsigned ipc = *(volatile u16 *)0x04000180 & 15;
    volatile u32 *saved = (volatile u32 *)0x06817ff0;
    unsigned code = 0xD5170000 | ((stage & 255)<<8) | ipc;
    if (*saved == code) return;
    if ((*saved & 0xffff0000) != 0xD5170000)
        for (unsigned i=0; i<256*192; i++) fb[i] = 0x8000;
    digit(fb, 100, 40, stage>>4, 0x83e0);
    digit(fb, 124, 40, stage, 0x83e0);
    digit(fb, 100, 100, ipc>>4, 0xffe0);
    digit(fb, 124, 100, ipc, 0xffe0);
    *saved = code;
}
