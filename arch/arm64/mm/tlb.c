#include <mmu.h>

/* ARM64 TLB 刷新：DSB + TLBI + DSB + ISB */
void arch_tlb_flush_all(void) {
  __asm__ volatile ("dsb sy" : : : "memory");
  __asm__ volatile ("tlbi vmalle1" : : : "memory");
  __asm__ volatile ("dsb sy" : : : "memory");
  __asm__ volatile ("isb" : : : "memory");
}
