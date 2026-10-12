/*
 * Exercise the recursive address macros without accessing page tables.
 * No privileges or particular running kernel layout are required.
 */
#include <sys/param.h>
#include <uvm/uvm_extern.h>
#include <machine/pmap.h>
#include <machine/vmparam.h>

#include <assert.h>
#include <stdio.h>

int pmap_pte_slot;

static void
check_address(vaddr_t va)
{
	assert((va >> 47) == 0x1ffff);
	assert(pl4_pi(va) == pmap_pte_slot);
	assert(va < VM_MIN_KERNEL_ADDRESS || va >= VM_MAX_KERNEL_ADDRESS);
}

int
main(void)
{
	unsigned int seed, direct, i, level, seen[PTE_SLOT_CHOICES] = { 0 };
	vaddr_t base[4], va, expected;

	for (seed = 0; seed < 256; seed++) {
		pmap_pte_slot = PTE_SLOT_MIN + (seed & PTE_SLOT_MASK);
		seen[pmap_pte_slot - PTE_SLOT_MIN]++;
		base[0] = (vaddr_t)L1_BASE;
		base[1] = (vaddr_t)L2_BASE;
		base[2] = (vaddr_t)L3_BASE;
		base[3] = (vaddr_t)L4_BASE;

		assert(base[0] == 0xffffa00000000000UL +
		    (pmap_pte_slot - 320) * (1UL << 39));
		check_address(base[0] + NBPD_L4 - 1);
		assert(pl4_pi(base[0]) != 255);
		assert(pl4_pi(base[0]) != L4_SLOT_EARLY);
		assert(pl4_pi(base[0]) != L4_SLOT_KERNBASE);
		assert(pl4_pi(base[0]) >= L4_SLOT_KERN + NKL4_MAX_ENTRIES);

		for (direct = 0; direct < DIRECT_MAP_START_CHOICES; direct++)
			assert(pmap_pte_slot < L4_SLOT_DIRECT + direct ||
			    pmap_pte_slot >= L4_SLOT_DIRECT + direct +
			    DIRECT_MAP_PML4_SLOTS);

		expected = 0xffff000000000000UL;
		for (level = 0; level < 4; level++) {
			expected |= (vaddr_t)pmap_pte_slot << (39 - level * 9);
			assert(base[level] == expected);
			check_address(base[level]);
		}
		assert((vaddr_t)PDP_PDE == expected + pmap_pte_slot * 8);

		/* Each level's entries must be reachable through the PTE array. */
		for (i = 0; i < 512; i++) {
			va = (vaddr_t)i << 39;
			if (i >= 256)
				va |= 0xffff000000000000UL;
			va |= (vaddr_t)i << 30 | (vaddr_t)i << 21 |
			    (vaddr_t)i << 12;
			for (level = 0; level < 3; level++) {
				va = base[0] + pl1_i(va) * sizeof(pt_entry_t);
				check_address(va);
			}
			assert(va == base[2] + (i * 512 + i) * 8);
			va = base[0] + pl1_i(va) * sizeof(pt_entry_t);
			assert(va == base[3] + i * 8);
		}
	}
	for (i = 0; i < PTE_SLOT_CHOICES; i++)
		assert(seen[i] == 2);
	puts("recursive layout: all 256 seeds and direct-map placements passed");
	return 0;
}
