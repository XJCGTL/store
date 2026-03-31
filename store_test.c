#include <am.h>
#include <klib.h>

#define PAGE_SIZE 4096
#define LOOP_COUNT 100  // 增加循环次数以平滑抖动
#define WARMUP_RUNS 2    // 预热运行次数
#define TEST_RUNS 5      // 正式测试运行次数

// 读取周期计数器
static inline uint64_t read_cycles() {
    uint64_t val;
    asm volatile("rdcycle %0" : "=r"(val));
    return val;
}

// 全局变量，防止编译器优化掉地址计算
volatile uint32_t *g_old_misalign_addr, *g_young_split_addr;
volatile uint32_t *g_aligned_addr1, *g_aligned_addr2;
volatile uint32_t *g_aligned_addr3, *g_aligned_addr4;

// --- 测试函数 ---

// 1. 对照组：对齐 -> 对齐
uint64_t measure_normal() {
    uint64_t start, end;
    register uint32_t store_val = 0xDEADBEEF;
    start = read_cycles();
    for (int i = 0; i < LOOP_COUNT; i++) {
        asm volatile (
            /* 延时：fdiv.s 高延迟，令第一条sw的地址依赖其结果 */
            "fmv.w.x ft0, %2\n\t"
            "fmv.w.x ft1, %2\n\t"
            "fdiv.s ft2, ft0, ft1\n\t"
            "fmv.x.w t4, ft2\n\t"
            "andi t4, t4, 0\n\t"
            "add t5, %0, t4\n\t"
            /* 第一条store指令，地址依赖fdiv延时链 */
            "sw %2, 0(t5)\n\t"
            /* 两条store指令之间插入算术指令（保证仍可进入同一ROB） */
            "add t0, %2, %2\n\t"
            "xor t1, t0, %2\n\t"
            "add t0, t1, t1\n\t"
            /* 第二条store指令 */
            "sw %2, 0(%1)\n\t"
            : : "r"(g_aligned_addr1), "r"(g_aligned_addr2), "r"(store_val)
            : "memory", "ft0", "ft1", "ft2", "t0", "t1", "t4", "t5"
        );
    }
    end = read_cycles();
    return (end - start) / LOOP_COUNT;
}

// 2. 冲突组：未对齐 -> 跨页
uint64_t measure_conflict() {
    uint64_t start, end;
    register uint32_t store_val = 0xDEADBEEF;
    start = read_cycles();
    for (int i = 0; i < LOOP_COUNT; i++) {
        asm volatile (
            /* 延时：fdiv.s 高延迟，令第一条sw的地址依赖其结果 */
            "fmv.w.x ft0, %2\n\t"
            "fmv.w.x ft1, %2\n\t"
            "fdiv.s ft2, ft0, ft1\n\t"
            "fmv.x.w t4, ft2\n\t"
            "andi t4, t4, 0\n\t"
            "add t5, %0, t4\n\t"
            /* 第一条store指令，地址依赖fdiv延时链 */
            "sw %2, 0(t5)\n\t"
            /* 两条store指令之间插入算术指令（保证仍可进入同一ROB） */
            "add t0, %2, %2\n\t"
            "xor t1, t0, %2\n\t"
            "add t0, t1, t1\n\t"
            /* 第二条store指令 */
            "sw %2, 0(%1)\n\t"
            : : "r"(g_old_misalign_addr), "r"(g_young_split_addr), "r"(store_val)
            : "memory", "ft0", "ft1", "ft2", "t0", "t1", "t4", "t5"
        );
    }
    end = read_cycles();
    return (end - start) / LOOP_COUNT;
}

// 3. 基线组 1: 未对齐 -> 对齐
uint64_t measure_baseline_misalign() {
    uint64_t start, end;
    register uint32_t store_val = 0xDEADBEEF;
    start = read_cycles();
    for (int i = 0; i < LOOP_COUNT; i++) {
        asm volatile (
            /* 延时：fdiv.s 高延迟，令第一条sw的地址依赖其结果 */
            "fmv.w.x ft0, %2\n\t"
            "fmv.w.x ft1, %2\n\t"
            "fdiv.s ft2, ft0, ft1\n\t"
            "fmv.x.w t4, ft2\n\t"
            "andi t4, t4, 0\n\t"
            "add t5, %0, t4\n\t"
            /* 第一条store指令，地址依赖fdiv延时链 */
            "sw %2, 0(t5)\n\t"
            /* 两条store指令之间插入算术指令（保证仍可进入同一ROB） */
            "add t0, %2, %2\n\t"
            "xor t1, t0, %2\n\t"
            "add t0, t1, t1\n\t"
            /* 第二条store指令 */
            "sw %2, 0(%1)\n\t"
            : : "r"(g_old_misalign_addr), "r"(g_aligned_addr3), "r"(store_val)
            : "memory", "ft0", "ft1", "ft2", "t0", "t1", "t4", "t5"
        );
    }
    end = read_cycles();
    return (end - start) / LOOP_COUNT;
}

// 4. 基线组 2: 对齐 -> 跨页
uint64_t measure_baseline_split() {
    uint64_t start, end;
    register uint32_t store_val = 0xDEADBEEF;
    start = read_cycles();
    for (int i = 0; i < LOOP_COUNT; i++) {
        asm volatile (
            /* 延时：fdiv.s 高延迟，令第一条sw的地址依赖其结果 */
            "fmv.w.x ft0, %2\n\t"
            "fmv.w.x ft1, %2\n\t"
            "fdiv.s ft2, ft0, ft1\n\t"
            "fmv.x.w t4, ft2\n\t"
            "andi t4, t4, 0\n\t"
            "add t5, %0, t4\n\t"
            /* 第一条store指令，地址依赖fdiv延时链 */
            "sw %2, 0(t5)\n\t"
            /* 两条store指令之间插入算术指令（保证仍可进入同一ROB） */
            "add t0, %2, %2\n\t"
            "xor t1, t0, %2\n\t"
            "add t0, t1, t1\n\t"
            /* 第二条store指令 */
            "sw %2, 0(%1)\n\t"
            : : "r"(g_aligned_addr4), "r"(g_young_split_addr), "r"(store_val)
            : "memory", "ft0", "ft1", "ft2", "t0", "t1", "t4", "t5"
        );
    }
    end = read_cycles();
    return (end - start) / LOOP_COUNT;
}


int main() {
    printf("Starting verification with strict measurement...\n");

    // --- 内存准备 ---
    if ((uintptr_t)_heap.end - (uintptr_t)_heap.start < PAGE_SIZE * 5) {
        printf("Error: Not enough heap space.\n"); return 1;
    }
    char *buffer = (char *)_heap.start;
    memset(buffer, 0, PAGE_SIZE * 5);

    // 检查首地址是否为整数页的首地址
    if ((uintptr_t)buffer % PAGE_SIZE != 0) {
        printf("Error: Buffer start address is not page-aligned (addr=0x%lx).\n", (unsigned long)(uintptr_t)buffer);
        return 1;
    }

    // 初始化全局地址指针
    g_old_misalign_addr = (uint32_t *)(buffer + PAGE_SIZE - 2);
    g_young_split_addr = (uint32_t *)(buffer + PAGE_SIZE - 2);
    g_aligned_addr1 = (uint32_t *)(buffer + PAGE_SIZE * 2 + 16);
    g_aligned_addr2 = (uint32_t *)(buffer + PAGE_SIZE * 2 + 20);
    g_aligned_addr3 = (uint32_t *)(buffer + PAGE_SIZE * 3 + 16);
    g_aligned_addr4 = (uint32_t *)(buffer + PAGE_SIZE * 4 + 16);
    
    // --- 预热阶段 ---
    printf("Warming up caches and TLB...\n");
    for (int i = 0; i < WARMUP_RUNS; i++) {
        measure_normal();
        measure_conflict();
        measure_baseline_misalign();
        measure_baseline_split();
    }

    // --- 正式测量阶段 ---
    printf("Starting formal measurement...\n");
    uint64_t total_normal = 0, total_conflict = 0;
    uint64_t total_base_misalign = 0, total_base_split = 0;

    for (int i = 0; i < TEST_RUNS; i++) {
        total_normal += measure_normal();
        total_conflict += measure_conflict();
        total_base_misalign += measure_baseline_misalign();
        total_base_split += measure_baseline_split();
    }

    // --- 结果计算与分析 ---
    uint64_t avg_normal = total_normal / TEST_RUNS;
    uint64_t avg_conflict = total_conflict / TEST_RUNS;
    uint64_t avg_base_misalign = total_base_misalign / TEST_RUNS;
    uint64_t avg_base_split = total_base_split / TEST_RUNS;

    printf("\n--- Measurement Results (cycles/iteration) ---\n");
    printf("1. [Normal]   Aligned -> Aligned:  %d\n", (int)avg_normal);
    printf("2. [Conflict] Misalign -> Split:   %d\n", (int)avg_conflict);
    printf("3. [Baseline] Misalign -> Aligned: %d\n", (int)avg_base_misalign);
    printf("4. [Baseline] Aligned -> Split:    %d\n", (int)avg_base_split);

    // 计算基线惩罚
    uint64_t misalign_penalty = avg_base_misalign > avg_normal ? avg_base_misalign - avg_normal : 0;
    uint64_t split_penalty = avg_base_split > avg_normal ? avg_base_split - avg_normal : 0;
    uint64_t expected_conflict_baseline = avg_normal + misalign_penalty + split_penalty;

    printf("\n--- Analysis ---\n");
    printf("Baseline penalty for a single Misaligned Store: %d cycles\n", (int)misalign_penalty);
    printf("Baseline penalty for a single Split Store:      %d cycles\n", (int)split_penalty);
    printf("Expected latency for Conflict (if no extra hazard): ~%d cycles\n", (int)expected_conflict_baseline);

    // 严格判断
    // 额外惩罚 = 实际冲突耗时 - 预期基线耗时
    int64_t extra_penalty = avg_conflict - expected_conflict_baseline;
    
    // 阈值可以设置得比较小，比如超过 10-20 个周期就认为有显著的额外惩罚
    if (extra_penalty > 20) {
        printf("\nRESULT: Mechanism DETECTED.\n");
        printf("An extra penalty of ~%d cycles was observed, which is beyond the sum of individual penalties.\n", (int)extra_penalty);
    } else {
        printf("\nRESULT: Mechanism NOT detected.\n");
        printf("The conflict case latency (%d) is close to the expected sum of baseline penalties (%d).\n", (int)avg_conflict, (int)expected_conflict_baseline);
    }

    return 0;
}
