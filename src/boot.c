#include "app_partition.h"
#include "boot.h"

#include <cmsis_core.h>
#include <zephyr/cache.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_DECLARE(zuf2, CONFIG_ZUF2_LOG_LEVEL);

#define SRAM_NODE DT_CHOSEN(zephyr_sram)
#define SRAM_BASE DT_REG_ADDR(SRAM_NODE)
#define SRAM_SIZE DT_REG_SIZE(SRAM_NODE)
#define SRAM_END (SRAM_BASE + SRAM_SIZE)

#define APP_BASE ZUF2_APP_PARTITION_ADDRESS
#define APP_SIZE ZUF2_APP_PARTITION_SIZE
#define APP_END (APP_BASE + APP_SIZE)

static int read_app_vectors(uint32_t *sp, uint32_t *reset)
{
	const volatile uint32_t *vectors = (const volatile uint32_t *)APP_BASE;

	/* The application is executed in place, so read vectors from that mapping. */
	*sp = vectors[0];
	*reset = vectors[1];

	return 0;
}

bool zuf2_app_is_valid(void)
{
	uint32_t sp;
	uint32_t reset;
	uint32_t reset_addr;

	if (read_app_vectors(&sp, &reset) != 0) {
		return false;
	}

	if (sp == 0xffffffffU || reset == 0xffffffffU || sp == 0U || reset == 0U) {
		return false;
	}

	if ((sp < SRAM_BASE) || (sp > SRAM_END) || ((sp & 0x7U) != 0U)) {
		LOG_WRN("invalid app stack pointer 0x%08x", sp);
		return false;
	}

	if ((reset & 0x1U) == 0U) {
		LOG_WRN("invalid app reset vector 0x%08x", reset);
		return false;
	}

	reset_addr = reset & ~0x1U;
	if ((reset_addr < APP_BASE) || (reset_addr >= APP_END)) {
		LOG_WRN("app reset vector 0x%08x outside app partition", reset);
		return false;
	}

	return true;
}

void zuf2_boot_app(void)
{
	uint32_t sp;
	uint32_t reset;

	if (read_app_vectors(&sp, &reset) != 0) {
		return;
	}

	LOG_INF("booting app at 0x%08x", APP_BASE);
	k_busy_wait(2000);

	__disable_irq();

	for (size_t i = 0; i < ARRAY_SIZE(NVIC->ICER); i++) {
		NVIC->ICER[i] = 0xffffffffU;
	}

	for (size_t i = 0; i < ARRAY_SIZE(NVIC->ICPR); i++) {
		NVIC->ICPR[i] = 0xffffffffU;
	}

	SysTick->CTRL = 0;
	SysTick->LOAD = 0;
	SysTick->VAL = 0;
	SCB->ICSR |= SCB_ICSR_PENDSTCLR_Msk;

#ifndef CONFIG_CPU_CORTEX_M0
	SCB->SHCSR &= ~(SCB_SHCSR_USGFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk |
			SCB_SHCSR_MEMFAULTENA_Msk);
#endif

	if ((CONTROL_SPSEL_Msk & __get_CONTROL()) != 0U) {
		__set_CONTROL(__get_CONTROL() & ~CONTROL_SPSEL_Msk);
	}

	__DSB();
	__ISB();

#if defined(CONFIG_CPU_CORTEX_M_HAS_VTOR)
	SCB->VTOR = APP_BASE;
#endif
	(void)sys_cache_instr_invd_all();

#if defined(CONFIG_CPU_CORTEX_M_HAS_SPLIM)
	__set_PSPLIM(0);
	__set_MSPLIM(0);
#endif

	__set_PSP(0);
	__asm__ volatile(
		"msr msp, %[app_sp]\n"
		"dsb 0xf\n"
		"isb 0xf\n"
		"bx %[app_reset]\n"
		:
		: [app_sp] "r"(sp), [app_reset] "r"(reset)
		: "memory");
	CODE_UNREACHABLE;
}
