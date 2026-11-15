// MIT License - see LICENSE
#include "board.h"

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_l2.h>
#include <zephyr/version.h>

// Symbols of the linker script: the end of what the image takes of the flash, and the limits of its RAM.
extern "C" {
extern char _flash_used[];
extern char _image_ram_start[];
extern char _image_ram_end[];
}

namespace {

// 1498 + 14 bytes of Ethernet header is a multiple of four, which the driver needs (see board.h).
constexpr unsigned short kMtu = 1498;

void limit(struct net_if *iface, void *) {
#if defined(CONFIG_NET_L2_ETHERNET)
    if (net_if_l2(iface) == &NET_L2_GET_NAME(ETHERNET))
        (void)net_if_set_mtu(iface, kMtu);
#else
    (void)iface;
#endif
}

void countThread(const struct k_thread *, void *count) { ++*static_cast<int *>(count); }

const char *cpuName() {
#if defined(CONFIG_CPU_CORTEX_M0)
    return "ARM Cortex-M0";
#elif defined(CONFIG_CPU_CORTEX_M0PLUS)
    return "ARM Cortex-M0+";
#elif defined(CONFIG_CPU_CORTEX_M3)
    return "ARM Cortex-M3";
#elif defined(CONFIG_CPU_CORTEX_M4)
    return "ARM Cortex-M4";
#elif defined(CONFIG_CPU_CORTEX_M7)
    return "ARM Cortex-M7";
#elif defined(CONFIG_CPU_CORTEX_M33)
    return "ARM Cortex-M33";
#elif defined(CONFIG_CPU_CORTEX_A53)
    return "ARM Cortex-A53";
#else
    return CONFIG_ARCH;
#endif
}

const char *architectureName() {
#if defined(CONFIG_CPU_CORTEX_M0) || defined(CONFIG_CPU_CORTEX_M0PLUS)
    return "Armv6-M";
#elif defined(CONFIG_CPU_CORTEX_M3) || defined(CONFIG_CPU_CORTEX_M4) || defined(CONFIG_CPU_CORTEX_M7)
    return "Armv7-M";
#elif defined(CONFIG_CPU_CORTEX_M33)
    return "Armv8-M";
#else
    return CONFIG_ARCH;
#endif
}

} // namespace

void limitEthernetFrames() { net_if_foreach(&limit, nullptr); }

long long uptimeMs() { return k_uptime_get(); }

BoardInfo boardInfo() {
    BoardInfo b{};
    b.cpu = cpuName();
    b.architecture = architectureName();
    b.cores = CONFIG_MP_MAX_NUM_CPUS;
    b.clockHz = CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC;
#if defined(CONFIG_CPU_HAS_FPU)
    b.fpu = true;
#endif
#if defined(CONFIG_CPU_HAS_ARM_MPU) || defined(CONFIG_CPU_HAS_NXP_SYSMPU)
    b.mpu = true;
#endif
#if defined(CONFIG_QEMU_TARGET)
    b.emulated = true;
#endif
    b.board = CONFIG_BOARD_TARGET;
    b.soc = CONFIG_SOC;

    b.flashSize = static_cast<long long>(CONFIG_FLASH_SIZE) * 1024;
    b.flashUsed = reinterpret_cast<long long>(_flash_used);
    b.ramSize = static_cast<long long>(CONFIG_SRAM_SIZE) * 1024;
    b.ramImage = _image_ram_end - _image_ram_start;
#if defined(CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE)
    b.libcHeap = CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE;
#else
    b.libcHeap = -1;
#endif
    b.kernelHeap = CONFIG_HEAP_MEM_POOL_SIZE;
    b.mainStack = CONFIG_MAIN_STACK_SIZE;
    b.mainStackPeak = -1;
#if defined(CONFIG_INIT_STACKS) && defined(CONFIG_THREAD_STACK_INFO)
    size_t unused = 0;
    if (k_thread_stack_space_get(k_current_get(), &unused) == 0)
        b.mainStackPeak = static_cast<long long>(CONFIG_MAIN_STACK_SIZE) - static_cast<long long>(unused);
#endif
    b.isrStack = CONFIG_ISR_STACK_SIZE;
#if defined(CONFIG_NT_THREAD_STACK_SIZE)
    b.threadStack = CONFIG_NT_THREAD_STACK_SIZE;
#else
    b.threadStack = -1;
#endif

    int threads = 0;
    k_thread_foreach(&countThread, &threads);
    b.threads = threads;
    b.tickHz = CONFIG_SYS_CLOCK_TICKS_PER_SEC;

    b.zephyr = KERNEL_VERSION_STRING;
    b.compiler = __VERSION__;
    b.libc = "picolibc";
    b.cppStandard = __cplusplus;

#if defined(CONFIG_NET_PKT_RX_COUNT)
    b.rxPackets = CONFIG_NET_PKT_RX_COUNT;
    b.txPackets = CONFIG_NET_PKT_TX_COUNT;
    b.rxBuffers = CONFIG_NET_BUF_RX_COUNT;
    b.txBuffers = CONFIG_NET_BUF_TX_COUNT;
    b.bufferSize = CONFIG_NET_BUF_DATA_SIZE;
#endif
    return b;
}
