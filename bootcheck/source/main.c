#include <nds.h>
#include <stdio.h>

// No FAT, keyboard, Wi-Fi, Maxmod, or YouTube code is linked here.
int main(void) {
    defaultExceptionHandler();
    lcdMainOnTop();
    setBrightness(3, 0);
    consoleDemoInit();
#ifdef BOOT_COMBO
    printf("YouTube DSi CORE CHECK\nARM7: Wi-Fi + Maxmod\n\nARM9 reached main\nDSi mode: %s\n\nARM9 starts no SD/network\n\nA: button test\nSTART: return to loader\n", isDSiMode() ? "yes" : "NO");
#elif defined(BOOT_WIFI)
    printf("YouTube DSi WIFI CORE\nARM7: Wi-Fi only\n\nARM9 reached main\nDSi mode: %s\n\nNo connection started\n\nA: button test\nSTART: return to loader\n", isDSiMode() ? "yes" : "NO");
#elif defined(BOOT_AUDIO)
    printf("YouTube DSi AUDIO CORE\nARM7: Maxmod only\n\nARM9 reached main\nDSi mode: %s\n\nNo audio stream started\n\nA: button test\nSTART: return to loader\n", isDSiMode() ? "yes" : "NO");
#else
    printf("YouTube DSi BOOT CHECK\n\nARM9 reached main\nDSi mode: %s\n\nNo SD / Wi-Fi / audio\n\nA: button test\nSTART: return to loader\n", isDSiMode() ? "yes" : "NO");
#endif
    unsigned frames = 0, count = 0;
    while (1) {
        cothread_yield_irq(IRQ_VBLANK);
        scanKeys();
        unsigned pressed = keysDown();
        if (pressed & KEY_START) return 0;
        if (pressed & KEY_A) count++;
        if ((frames++ % 60) == 0 || (pressed & KEY_A))
            printf("\x1b[12;0HSeconds: %u\nA presses: %u\n", frames / 60, count);
    }
}
