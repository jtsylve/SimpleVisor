/*++

Copyright (c) Joe T. Sylve.  All rights reserved.

Module Name:

	shvvmxept.c

Abstract:

	This module implements Intel VMX EPT (Extended Page Tables)-specific routines.

Author:

	Joe T. Sylve (@jtsylve) 16-Apr-2016 - Initial version

Environment:

	Kernel mode only, IRQL DISPATCH_LEVEL.

--*/

#include "shv.h"

// ===========================================================================
//
// MACROS
//
// ===========================================================================

//
// Convert a page frame number (PFN) to a physical address.
//
#define SHV_PFN_TO_PHYS(pfn) (pfn << PAGE_SHIFT)

//
// Convert a physical address to a page frame number (PFN).
//
#define SHV_PHYS_TO_PFN(pa) (pa >> PAGE_SHIFT)

//
// Iterate through each entry in a level of a page table.
//
#define SHV_FOR_EACH_ENTRY(table, name, type) \
	for (type name = table;\
		(ULONG_PTR)name < (ULONG_PTR)table + PAGE_SIZE;\
		name++\
		)

//
// Given a violation reasion, tell whether an EPT violation
// was caused by an EPT entry not being present.
//
#define SHV_EPT_VIOLATION_ENTRY_MISS(vr) ((vr & (7 << 3)) == 0)

// ===========================================================================
//
// GLOBAL DATA
//
// ===========================================================================

VMX_EPT_EPTP ShvVmxEptEptp = { 0 };

// ===========================================================================
//
// LOCAL DATA
//
// ===========================================================================

static PVMX_EPT_ENTRY ShvVmxEptPML4 = NULL;
static KSPIN_LOCK ShvVmxEptPML4Lock = { 0 };

// ===========================================================================
//
// LOCAL PROTOTYPES
//
// ===========================================================================

static PVOID
ShvVmxEptGetVirtualFromPfn(
	SIZE_T Pfn
);

static SIZE_T
ShvVmxEptGetPfnFromVirtual(
	PVOID Va
);

static NTSTATUS
_ShvVmxEptPopulateIdentityTable(
	PVMX_EPT_ENTRY table,
	ULONG level,
	PHYSICAL_ADDRESS address
);

static NTSTATUS
ShvVmxIdentityMapPage(
	PHYSICAL_ADDRESS address
);

static NTSTATUS
ShvVmxEptBuildIdentityTables(
	VOID
);

static VOID
ShvVmxEptInvalidateEpt(
	VOID
);

// ===========================================================================
//
// PUBLIC FUNCTIONS
//
// ===========================================================================

BOOLEAN
ShvVmxEptProbe(
	VOID
)
{
	INT64 control;

	//
	// Verify that secondary processor-based VM-execution
	// controls are used.
	//
	control = __readmsr(MSR_IA32_VMX_PROCBASED_CTLS);

	if (_bittest64(&control, 32 + 31) == 0)
	{
		// return FALSE;
	}

	//
	// Verify that EPT can be enabled.
	//
	control = __readmsr(MSR_IA32_VMX_PROCBASED_CTLS2);

	if (_bittest64(&control, 32 + 1) == 0)
	{
		return FALSE;
	}

	return TRUE;
}

NTSTATUS
ShvVmxEptInitialize(
	VOID
)
{
	NTSTATUS ret;

	//
	// Allocate memory to hold the EPT PML4 table.
	//
	ShvVmxEptPML4 = (PVMX_EPT_ENTRY)ShvUtilAllocateContiguousMemory(PAGE_SIZE);
	if (ShvVmxEptPML4 == NULL) {
		return STATUS_HV_NO_RESOURCES;
	}

	//
	// Zero the PML4 table.
	//
	__stosq((PUINT64)ShvVmxEptPML4, 0, PAGE_SIZE / sizeof(ULONG64));

	//
	// Initialize the lock.
	//
	KeInitializeSpinLock(&ShvVmxEptPML4Lock);

	//
	// Build the EPT identity table by creating an entry for
	// each physical address page on the system.
	//
	ret = ShvVmxEptBuildIdentityTables();
	if (ret != STATUS_SUCCESS)
	{
		ShvVmxEptCleanup();
		return ret;
	}

	//
	// Initialize the EPTP by setting the PFN of the top-level
	// EPT table as well as the number of page table levels - 1.
	//
	ShvVmxEptEptp.PFN = ShvVmxEptGetPfnFromVirtual(ShvVmxEptPML4);
	ShvVmxEptEptp.PW = VMX_EPT_PAGE_WALK_LENGTH - 1;
	ShvVmxEptEptp.MT = WriteBack;

	return STATUS_SUCCESS;
}

VOID
ShvVmxEptCleanup(
	VOID
)
{
	KeAcquireSpinLockAtDpcLevel(&ShvVmxEptPML4Lock);

	if (ShvVmxEptPML4 == NULL)
	{
		//
		// Nothing to do here.
		//
		KeReleaseSpinLockFromDpcLevel(&ShvVmxEptPML4Lock);
		return;
	}

	//
	// Iterate through each of the PML4 entries and free each
	// of the lower tables.
	//
	SHV_FOR_EACH_ENTRY(ShvVmxEptPML4, pml4e, PVMX_EPT_ENTRY)
	{
		PVMX_EPT_PDPTE pdpt;

		if (pml4e->QuadPart == 0)
		{
			//
			// Entry is not set
			//
			continue;
		}

		//
		// Get the virtual address for the PDP table from the PFN
		// in the PML4 entry.
		//
		pdpt = (PVMX_EPT_PDPTE)ShvVmxEptGetVirtualFromPfn(pml4e->PFN);

		//
		// Iterate through each of the PDPT entries and free each
		// of the lower tables.
		//
		SHV_FOR_EACH_ENTRY(pdpt, pdpte, PVMX_EPT_PDPTE)
		{
			PVMX_EPT_PDE pdt;

			if (pdpte->QuadPart == 0 || pdpte->P == 1)
			{
				//
				// Entry is either not set or isn't a pointer
				// to a lower directory table.
				//
				continue;
			}

			//
			// Get the virtual address for the PD table from the PFN
			// in the PDPT entry.
			//
			pdt = (PVMX_EPT_PDE)ShvVmxEptGetVirtualFromPfn(pdpte->Dir.PFN);

			//
			// Iterate through each of the PD entries and free each
			// of the lower tables.
			//
			SHV_FOR_EACH_ENTRY(pdt, pde, PVMX_EPT_PDE)
			{
				PVMX_EPT_PTE pt;

				if (pde->QuadPart == 0 || pde->P == 1)
				{
					//
					// Entry is either not set or isn't a pointer
					// to a lower directory table.
					//
					continue;
				}

				//
				// Get the virtual address for the page table from the PFN
				// in the PD entry.
				//
				pt = (PVMX_EPT_PTE)ShvVmxEptGetVirtualFromPfn(pde->Dir.PFN);

				//
				// Free the page table.
				//
				MmFreeContiguousMemory(pt);
			}

			//
			// Free the PD table.
			//
			MmFreeContiguousMemory(pdt);
		}

		//
		// Free the PDP table.
		//
		MmFreeContiguousMemory(pdpt);
	}

	//
	// Free the PML4 Table
	//
	MmFreeContiguousMemory(ShvVmxEptPML4);
	ShvVmxEptPML4 = NULL;

	KeReleaseSpinLockFromDpcLevel(&ShvVmxEptPML4Lock);
}

VOID
ShvVmxEptHandleViolation(
	_In_ PSHV_VP_STATE VpState
)
{
	UNREFERENCED_PARAMETER(VpState);

	PHYSICAL_ADDRESS gpa;
	SIZE_T eq;

	//
	// Read guest physical address that caused the violation.
	//
	__vmx_vmread(GUEST_PHYSICAL_ADDRESS, (PSIZE_T)&gpa.QuadPart);

	//
	// Read the exit qualification
	//
	__vmx_vmread(EXIT_QUALIFICATION, &eq);

	SHV_DEBUG_PRINT("[%u] GPA: %llx Exit Reason %llx\n",
		KeGetCurrentProcessorNumberEx(NULL),
		gpa.QuadPart,
		eq
	);

	//
	// Check to see if the violation was caused because there was no EPT
	// entry present.  This could happen, because we didn't identity map
	// the hardware MMIO mappings.
	//
	if (SHV_EPT_VIOLATION_ENTRY_MISS(eq)) {
		NTSTATUS ret;

		//
		// Add an EPT entry for the GPA.
		//
		ret = ShvVmxIdentityMapPage(gpa);
		NT_VERIFYMSG("GPA EPT Allocation Failed", ret == STATUS_SUCCESS);

		//
		// Since we modified the EPT table, we need to invalidate the EPT.
		//
		ShvVmxEptInvalidateEpt();

		return;
	}

	NT_ASSERTMSG("Unknown EPT Violation Reason", FALSE);
}

// ===========================================================================
//
// LOCAL FUNCTIONS
//
// ===========================================================================

static PVOID
ShvVmxEptGetVirtualFromPfn(
	SIZE_T Pfn
)
{
	PHYSICAL_ADDRESS pa;

	//
	// First convert the PFN to a physical address.
	//
	pa.QuadPart = SHV_PFN_TO_PHYS(Pfn);

	//
	// Convert the physical address to a virtual address.
	//
	return MmGetVirtualForPhysical(pa);
}

static SIZE_T
ShvVmxEptGetPfnFromVirtual(
	PVOID Va
)
{
	PHYSICAL_ADDRESS pa;

	//
	// First convert the virtual address to a physical address.
	//
	pa = MmGetPhysicalAddress(Va);

	//
	// Convert the physical address to a PFN.
	//
	return SHV_PHYS_TO_PFN(pa.QuadPart);
}

static NTSTATUS
_ShvVmxEptPopulateIdentityTable(
	PVMX_EPT_ENTRY table,
	ULONG level,
	PHYSICAL_ADDRESS address
)
{
	PVMX_EPT_ENTRY next;
	VMX_EPT_ADDRESS gpa, ta;

	// Initialize the guest physical address and the table address.
	gpa.QuadPart = address.QuadPart;
	ta.Entry = table;

	NT_ASSERT(level <= 4 && level >= 1);

	switch (level) {
	case 4: // PML4E
		ta.GPA = gpa.PML4E;
		break;
	case 3: // PDPTE
		ta.GPA = gpa.PDPTE;
		break;
	case 2: // PDE
		ta.GPA = gpa.PDE;
		break;
	case 1: // PTE
		ta.GPA = gpa.PTE;
		break;
	}

	//
	// If we're at the bottom level, we just need to populate
	// the PTE with the PFN of the HPA.
	//
	if (level == 1)
	{
		PVMX_EPT_PTE pte;

		pte = (PVMX_EPT_PTE)ta.Entry;

		//
		// Populate the PTE if it's not already set.
		//
		if (pte->QuadPart == 0)
		{
			pte->R = 1;
			pte->W = 1;
			pte->X = 1;
			pte->MT = WriteBack;
			pte->PFN = SHV_PHYS_TO_PFN(address.QuadPart);
		}

		return STATUS_SUCCESS;
	}

	// Let's check if we need to initialize the entry
	if (ta.Entry->QuadPart == 0) {
		//
		// Allocate memory to hold the table.
		//
		next = (PVMX_EPT_ENTRY)ShvUtilAllocateContiguousMemory(PAGE_SIZE);
		if (next == NULL) {
			return STATUS_HV_NO_RESOURCES;
		}

		//
		// Zero the table.
		//
		__stosq((PUINT64)next, 0, PAGE_SIZE / sizeof(ULONG64));

		ta.Entry->R = 1;
		ta.Entry->W = 1;
		ta.Entry->X = 1;
		ta.Entry->PFN = ShvVmxEptGetPfnFromVirtual(next);
	}
	else
	{
		next = (PVMX_EPT_ENTRY)ShvVmxEptGetVirtualFromPfn(ta.Entry->PFN);
	}

	return _ShvVmxEptPopulateIdentityTable(next, level - 1, address);
}

static NTSTATUS
ShvVmxIdentityMapPage(
	PHYSICAL_ADDRESS address
)
{
	NTSTATUS ret;

	KeAcquireSpinLockAtDpcLevel(&ShvVmxEptPML4Lock);

	NT_ASSERTMSG("PML4 is not allocated.", (ShvVmxEptPML4 != NULL));

	ret = _ShvVmxEptPopulateIdentityTable(ShvVmxEptPML4, VMX_EPT_PAGE_WALK_LENGTH, address);

	KeReleaseSpinLockFromDpcLevel(&ShvVmxEptPML4Lock);

	return ret;
}

static NTSTATUS
ShvVmxEptBuildIdentityTables(
	VOID
)
{
	PPHYSICAL_MEMORY_RANGE ranges;
	PHYSICAL_ADDRESS apicBase;
	NTSTATUS ret;

	//
	// Get physical memory ranges
	//
	ranges = MmGetPhysicalMemoryRanges();

	//
	// Iterate through each physical memory range and create an identity mapping
	// for each 4 KiB page in each range.
	//
	for (SIZE_T i = 0; ranges[i].BaseAddress.QuadPart != 0 || ranges[i].NumberOfBytes.QuadPart != 0; i++)
	{
		PHYSICAL_ADDRESS start, end;

		start.QuadPart = ranges[i].BaseAddress.QuadPart;
		end.QuadPart = start.QuadPart + ranges[i].NumberOfBytes.QuadPart - 1;

		//
		// Populate the EPT table with a PTE for each page in the range.
		//
		for (PHYSICAL_ADDRESS address = start; address.QuadPart < end.QuadPart; address.QuadPart += PAGE_SIZE)
		{
			ret = ShvVmxIdentityMapPage(address);
			if (ret != STATUS_SUCCESS) {
				return ret;
			}
		}
	}

	//
	// We also apparently have to create a mapping for the APIC or the system
	// hangs on us.
	//

	//
	// Get the APIC base physical address.
	//
	apicBase.QuadPart = (__readmsr(IA32_APIC_BASE_MSR) & IA32_APIC_BASE_ADDRESS_MASK);

	//
	// Map the APIC base page.
	//
	ret = ShvVmxIdentityMapPage(apicBase);
	if (ret != STATUS_SUCCESS) {
		return ret;
	}

	return STATUS_SUCCESS;
}

static VOID
ShvVmxEptInvalidateEpt(
	VOID
)
{
	//
	// Build the INVEPT descriptor.
	//
	struct {
		VMX_EPT_EPTP Eptp;
		ULONG64		 reserved0;
	} invdesc = { 0 };

	invdesc.Eptp = ShvVmxEptEptp;

	//
	// Invalidate the EPT
	//
	__vmx_invept(1, &invdesc);
}

