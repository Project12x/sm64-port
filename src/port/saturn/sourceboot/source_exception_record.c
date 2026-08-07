#include <stdint.h>

#include <cpu/registers.h>

/*
 * Yaul's exception trampoline passes the original register frame to
 * __exception_assert(), but __reset() and the VDP2 diagnostics then reuse the
 * faulting stack.  Keep one fixed, pointer-free copy in HWRAM so a debugger
 * can classify the original exception after the reset screen appears.
 */
typedef struct sourceboot_exception_record {
    uint32_t magic;
    uint32_t exception_name;
    cpu_registers_t regs;
} sourceboot_exception_record_t;

enum { SOURCEBOOT_EXCEPTION_RECORD_MAGIC = 0x53484258U };

volatile sourceboot_exception_record_t sourceboot_exception_record
    __attribute__((used, section(".bss")));

extern void __exception_assert(const cpu_registers_t *regs,
                               const char *exception_name);

void sourceboot_exception_assert(const cpu_registers_t *regs,
                                 const char *exception_name)
{
    sourceboot_exception_record_t *record =
        (sourceboot_exception_record_t *)&sourceboot_exception_record;

    record->magic = 0U;
    record->exception_name = (uint32_t)(uintptr_t)exception_name;
    if (regs != 0) {
        record->regs = *regs;
    } else {
        record->regs = (cpu_registers_t){0};
    }
    record->magic = SOURCEBOOT_EXCEPTION_RECORD_MAGIC;

    __exception_assert(regs, exception_name);
}
