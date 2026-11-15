// MIT License - see LICENSE
#pragma once

// Board-specific parts of the sample, kept apart from the Qt code because they need the Zephyr headers.

// Sets the MTU of the Ethernet interfaces to 1498, so that Zephyr's driver for the LAN9118 can receive the
// largest frame that results.
//
// Zephyr 4.5's driver for that controller, which QEMU emulates, reads a frame rounded up to whole 32-bit
// words, into a packet whose size Zephyr limits to the MTU plus the 14-byte Ethernet header. A frame of 1514
// bytes, the largest at the usual MTU of 1500, is not a multiple of four, so the last words do not fit; the
// driver then "discards" the frame in a way that leaves the controller stuck, and the board never receives
// another packet. A browser causes this at once: its request, with the cookies it holds for localhost, fills
// a frame. With an MTU of 1498 the largest frame is 1512 bytes, a multiple of four, and every frame fits.
// A board with another Ethernet controller needs none of this.
void limitEthernetFrames();

// What the sample knows about the processor and the resources of the board it runs on. A size or a count that
// is not known is -1.
struct BoardInfo {
    // Processor.
    const char *cpu;          // "ARM Cortex-M3"
    const char *architecture; // "Armv7-M"
    int cores;
    long long clockHz; // of the system clock source
    bool fpu;
    bool mpu;
    bool emulated;     // running under QEMU
    const char *board; // "mps2/an385"
    const char *soc;   // "an385"
    // Memory, in bytes.
    long long flashSize;
    long long flashUsed;
    long long ramSize;
    long long ramImage;   // what the image takes of the RAM, the heaps included
    long long libcHeap;   // arena of malloc(), where Nt allocates from
    long long kernelHeap; // k_malloc(), which holds the stacks of the threads Nt starts
    long long mainStack;
    long long mainStackPeak; // most of it that was ever used
    long long isrStack;
    long long threadStack; // of each thread that Nt starts
    // Kernel.
    int threads;
    int tickHz;
    // Software.
    const char *zephyr;   // "4.5.0"
    const char *compiler; // "14.3.0 (Zephyr SDK 1.0.1)"
    const char *libc;     // "picolibc"
    long cppStandard;     // __cplusplus
    // Network buffers.
    int rxPackets, txPackets;
    int rxBuffers, txBuffers;
    int bufferSize;
};

BoardInfo boardInfo();

// Milliseconds since the kernel started counting, that is since the board was reset.
long long uptimeMs();
