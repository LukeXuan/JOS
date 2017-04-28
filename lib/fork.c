// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//

static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.

	if (! (err & FEC_WR) || ! (uvpt[PGNUM(addr)] & PTE_COW)) {
		panic("Unhandled page fault: %d %d", (err & FEC_WR), (uvpt[PGNUM(addr)] & PTE_COW));
	}

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.

	addr = ROUNDDOWN(addr, PGSIZE);
	r = sys_page_alloc(0, (void *)PFTEMP, PTE_U | PTE_W | PTE_P);
	if (r < 0) {
		panic("Exception page allocation failed: %e", r);
	}
	memmove(PFTEMP, addr, PGSIZE);
	r = sys_page_map(0, (void *)PFTEMP, 0, addr, PTE_U | PTE_W | PTE_P);
	if (r < 0) {
		panic("Exception when mapping page: %e", r);
	}
	r = sys_page_unmap(0, PFTEMP);
	if (r < 0) {
		panic("Exception when unmapping page: %e", r);
	}
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;
	uint32_t perm;

	uintptr_t va = pn << PGSHIFT;

	if ((uvpt[pn] & PTE_W) || (uvpt[pn] & PTE_COW)) {
		perm = PTE_P | PTE_COW | PTE_U;
		r = sys_page_map(0, (void *)va, envid, (void *)va, perm);
		if (r < 0) {
			return r;
		}
		r = sys_page_map(0, (void *)va, 0, (void *)va, perm);
		if (r < 0) {
			return r;
		}
	}
	else {
		r = sys_page_map(0, (void *)va, 0, (void *)va, uvpt[pn] & PTE_SYSCALL);
		if (r < 0) {
			return r;
		}
	}

	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	size_t n;
	int32_t envid, ret;
	uintptr_t va;
	extern void _pgfault_upcall(void);

	set_pgfault_handler(pgfault);

	envid = sys_exofork();

	if (envid < 0) {
		return envid;
	}

	if(envid > 0) {
		// The _pgfault_handler is also copied
		for(va = 0; va < UTOP; va += PGSIZE) {
			if (va == UXSTACKTOP - PGSIZE) {
				// we are at the exception stack
				continue;
			}
			// Check uvpd PTE_P first, pt may not exist
			if ((uvpd[PDX(va)] & PTE_P) && (uvpt[PGNUM(va)] & PTE_P)) {
				ret = duppage(envid, PGNUM(va));
				if (ret < 0) {
					panic("Duplicate page failed: %e", ret);
				}
			}
		}

		ret = sys_page_alloc(envid, (void *)UXSTACKTOP - PGSIZE, PTE_W | PTE_U | PTE_P);
		if (ret < 0) {
			panic("Exception stack allocation failed: %e", ret);
		}

		ret = sys_env_set_pgfault_upcall(envid, _pgfault_upcall);
		if (ret < 0) {
			panic("Exception when setting up pgfault handler: %e", ret);
		}

		ret = sys_env_set_status(envid, ENV_RUNNABLE);
		if (ret < 0) {
			panic("Exception when setting up status: %e", ret);
		}
	}
	else {
		thisenv = envs + ENVX(sys_getenvid());
	}

	return envid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
