# Lab3

本实验所需源码可从 [lzu_oslab_exp 仓库](https://git.neko.ooo/LZU-OSLab/lzu_oslab_exp) 下载。

本实验的 step by step 文档可以访问 [Code an OS Project](https://lzu-oslab.github.io/step_by_step_doc/)。

这一节我们将完成物理内存的管理。

## 物理内存的布局

通常，我们将内存看成是一个字节数组，地址是数组的下标。但实际上这个模型只能算是内存的抽象，真实的物理地址空间不一定从 0 开始寻址，也不一定是连续的。

物理内存地址空间是物理内存、内存映射 I/O 和一些 ROM 的集合，其中的地址不一定都指向物理内存，地址空间中还存在许多空洞。这里介绍 QEMU 模拟器的物理地址空间布局：

```c
// https://github.com/qemu/qemu/blob/master/hw/riscv/virt.c
static const struct MemmapEntry {
    hwaddr base;
    hwaddr size;
} virt_memmap[] = {
    [VIRT_DEBUG] =        {        0x0,         0x100 },
    [VIRT_MROM] =         {     0x1000,        0xf000 },
    [VIRT_TEST] =         {   0x100000,        0x1000 },
    [VIRT_RTC] =          {   0x101000,        0x1000 },
    [VIRT_CLINT] =        {  0x2000000,       0x10000 },
    [VIRT_ACLINT_SSWI] =  {  0x2F00000,        0x4000 },
    [VIRT_PCIE_PIO] =     {  0x3000000,       0x10000 },
    [VIRT_PLATFORM_BUS] = {  0x4000000,     0x2000000 },
    [VIRT_PLIC] =         {  0xc000000, VIRT_PLIC_SIZE(VIRT_CPUS_MAX * 2) },
    [VIRT_APLIC_M] =      {  0xc000000, APLIC_SIZE(VIRT_CPUS_MAX) },
    [VIRT_APLIC_S] =      {  0xd000000, APLIC_SIZE(VIRT_CPUS_MAX) },
    [VIRT_UART0] =        { 0x10000000,         0x100 },
    [VIRT_VIRTIO] =       { 0x10001000,        0x1000 },
    [VIRT_FW_CFG] =       { 0x10100000,          0x18 },
    [VIRT_FLASH] =        { 0x20000000,     0x4000000 },
    [VIRT_IMSIC_M] =      { 0x24000000, VIRT_IMSIC_MAX_SIZE },
    [VIRT_IMSIC_S] =      { 0x28000000, VIRT_IMSIC_MAX_SIZE },
    [VIRT_PCIE_ECAM] =    { 0x30000000,    0x10000000 },
    [VIRT_PCIE_MMIO] =    { 0x40000000,    0x40000000 },
    [VIRT_DRAM] =         { 0x80000000,           0x0 },
};
```

物理地址空间中的空洞没有任何意义，不能使用。除 RAM 外，物理地址空间中还有大量内存映射 I/O，已经在 lab2 实验中有所介绍。RISC-V 使用设备树（*device tree*）协议来描述硬件信息，启动后会扫描硬件（或加载指定设备树）并将结果以 DTB（*Device Tree Blob*）格式写入到物理内存中，其内存起始地址会在跳转至内核前被传入 a1 寄存器，这样内核就可以通过解析 DTB 来获取硬件信息。使用设备树探测硬件信息的粗略代码已在 lab2 出现过（见 `trap/dtb.c`），设备树协议的详细介绍可以阅读 [Device Tree（一）：背景介绍](http://www.wowotech.net/device_model/why-dt.html)。

```c
// include/mm.h: 定义物理内存空间相关的宏
#define PAGE_SIZE 4096
#define FLOOR(addr) ((addr) / PAGE_SIZE * PAGE_SIZE)/**< 向下取整到 4K 边界 */
#define CEIL(addr)               \
    (((addr) / PAGE_SIZE + ((addr) % PAGE_SIZE != 0)) * PAGE_SIZE) /**< 向上取整到 4K 边界 */
#define DEVICE_START    0x10000000                  /**< 设备树地址空间，暂时不使用 */
#define DEVICE_END      0x10010000
#define MEM_START       0x80000000                  /**< 物理内存地址空间 */
#define MEM_END         0x88000000
#define SBI_START       0x80000000                  /**< SBI 物理内存起始地址 */
#define SBI_END         0x80200000                  /**< 用户程序（包括内核）可用的物理内存地址空间开始 */
#define HIGH_MEM        0x88000000                  /**< 空闲内存区结束 */
#define LOW_MEM         0x82000000                  /**< 空闲内存区开始（可用于用户进程和数据放置） */
#define PAGING_MEMORY   (1024 * 1024 * 128)         /**< 系统物理内存大小 (bytes) */
#define PAGING_PAGES    (PAGING_MEMORY >> 12)       /**< 系统物理内存页数 */
```

本实验中，RAM 地址从 `0x8000_0000` 到 `0x8800_0000`，共 128M，其中系统内核之后的地址空间由用户进程使用。为了实现的简单，直接给内核分配了一段足够大的内存 [SBI_END, LOW_MEM)。注意，实验中的地址空间都是左闭右开的区间。

由于 QEMU 模拟器默认提供的内存布局和大小是固定的，本实验暂时跳过通过 DTB 检测物理内存的过程，直接管理物理内存。

## 物理页的管理

我们将物理内存管理起来是为了实现虚拟内存，虚拟内存以页为单位，因此我们也以页为单位管理物理内存。将物理地址空间按页划分，得到许多物理页，每个物理页都有自己的编号，这样物理地址空间就可以看成是一个以物理页为元素的数组，数组下标就是物理页的物理页号。

本实验使用位图（*bitmap*）管理 128M 物理内存，这也是 Linux 内核最初使用的物理内存管理方式。以数组 `mem_map[]` 表示 128M 物理内存，下标是物理页号，元素代表该物理页的引用计数。

```c
// include/mm.h
#define USED 100
#define UNUSED 0

extern unsigned char mem_map[PAGING_PAGES]; /**< 物理内存位图,记录物理内存使用情况 */
```

物理内存地址从 `0x8000_0000` 开始，显然，物理地址右移 12 位得到的物理页号不能直接用作 `mem_map[]` 的下标，须要把物理地址减去 `0x8000_0000` 再右移 12 位。

```c
// include/mm.h
#define MAP_NR(addr)    (((addr)-MEM_START) >> 12)  /**< 物理地址 addr 在 mem_map[] 中的下标 */
```

每当请求新的物理页时，内核就从位图 `mem_map[]` 查找到空闲物理页并分配。物理地址中有一部分被 SBI 与内核占据，这部分物理内存一定不能被分配，否则会导致内核和 SBI 被用户进程覆盖，因此须在 `mem_map[]` 中将这部分内存标记为 `USED`，剩余部分在初始化时须标记为 `UNUSED`。

lab3 实验中的地址都已经对齐到了 4K 边界，因此不须要再做对齐。

## 物理页的分配

物理页的分配就是在所有物理页中找到一个空闲（未分配）物理页。实验使用最简单的数据结构管理物理页，自然也使用最简单的方式分配物理页：遍历物理内存位图，查找空闲页并分配。

分配出来的物理页很可能要作为页表等关键的数据结构使用，因此在分配的过程中就须要将物理页清零。

## 物理页的释放

物理页的释放就是将某一物理页标记为空闲的（可分配的），以便下次分配。

我们会在后面的实验中实现虚拟内存，多个虚拟页可以通过某种方式共享（映射到）一个物理页，物理页的引用计数很可能会大于 1。每次“释放”物理页只须要递减引用计数，当引用计数为 0 时，物理页自然就空闲了，无须更多额外操作。

## 测试

物理内存的分配回收是后面所有模块的基础，一旦物理内存模块出错却没有被发现，在实现后面的虚拟内存、进程、文件系统时就会“写时一时爽，调试火葬场”。

为了验证模块的可靠性，我们编写了一个简单的测试函数`mem_test()`，检查是否正确初始化`mem_map[]`，是否正确分配回收物理页。

测试通过后可以看到`mem_test()`打印：

```
mem_test(): Passed
```

## 总结

我们通过软件实现了物理内存的管理，营造了“物理内存可以增减、分配、释放”的假象。当需要物理内存时，通过 `get_free_page()` 获取一页，不需要时通过 `free_page()` 释放物理页。如果软件使用了全部的物理内存，虽然系统有 128M 物理内存，但对于软件来说，物理内存已经耗尽，可用的物理内存为 0 字节。

**注意：**从 lab3 起我们提供了 `panic` 与 `assert` 宏以便调试和系统警告，具体用法请见 `include/assert.h` 注释，此处不再赘述。

## 实验内容

1. 请将你在 Lab1 中填充的代码复制到 Lab3 中：
    - Lab1 中 `entry.s` 对应 Lab3 的文件为 `init/entry.s`
    - Lab1 中 `sbi.c` 对应 Lab3 的文件为 `lib/sbi.c`
2. **根据上述解析、参考文档、代码注释与关联内容代码**，补全以下文件中的代码，使得系统启动后运行的物理内存测试可正常通过：
    1. mm/memory.c 中的 `mem_init`, `free_page`, `get_free_page` 函数
    2. lib/string.c 中的 `memset` 函数
3. 回答以下问题：
   1. 尝试访问 `0x80000xxx` 内存区，观察现象，分析访问不成功的原因。
4. 拓展任务（本次拓展任务 2、3 难度较大）：
    1. 实现一个连续物理内存页分配器 `uint64_t get_muti_page(size_t page_num)`, `void free_muti_page(uint64_t start_address, size_t page_num)`
    2. 实现伙伴系统 `uint64_t falloc(size_t size)`, `uint64_t ffree(uint64_t start_address)`
    3. 实现任意 2 的幂次大小内存分配 `uint64_t malloc(size_t size)`, `uint64_t free(uint64_t start_address)`
    4. 本章实验内容有何缺陷与可改进之处？
