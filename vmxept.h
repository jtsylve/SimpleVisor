/*++

Copyright (c) Joe T. Sylve.  All rights reserved.

Header Name:

vmxept.h

Abstract:

This header defines the structures for Intel x64 VT-x EPT support.

Author:

Joe T. Sylve (@jtsylve) 16-Apr-2016 - Initial version

Environment:

Kernel mode only.

--*/

#pragma once

//
// The EPT memory type is specified in bits 5:3 of the last EPT
// paging-structure entry.  Other values are reserved and cause
// EPT misconfigurations.
//
typedef enum _VMX_EPT_MEMORY_TYPE {
	Uncacheable,
	WriteCombining,
	WriteThrough = 4,
	WriteProtected,
	WriteBack,
	Uncached,
} VMX_EPT_MEMORY_TYPE, *PVMX_EPT_MEMORY_TYPE;

//
// The extended-page-table pointer (EPTP) contains the 
// address of the base of EPT PML4 table as well as 
// other EPT configuration information.
//
typedef union _VMX_EPT_EPTP {
	struct {
		ULONG64 MT : 3; // EPT paging-structure memory type
		ULONG64 PW : 3; // This value is 1 less than the EPT page-walk length
		ULONG64 ADE : 1; // Setting to 1 enables accessed and dirty flags for EPT
		ULONG64 reserved0 : 5;
		ULONG64 PFN : 52; // The physical address of the 4 KiB aligned EPT PML4 table
	};
	ULONG64 QuadPart;
} VMX_EPT_EPTP, *PVMX_EPT_EPTP;
C_ASSERT(sizeof(VMX_EPT_EPTP) == 8);

//
// 4 KiB naturally aligned EPT PML4, PDPT, and PDE table entries
// are used to point to the next level tables. Each table comprises 
// 512 64-bit entries (EPT PML4Es).
//
typedef union _VMX_EPT_ENTRY {
	struct {
		ULONG64 R : 1; // Read access
		ULONG64 W : 1; // Write access
		ULONG64 X : 1; // Execute access
		ULONG64 reserved0 : 5;
		ULONG64 A : 1; //Indicates whether software has accessed the region
		ULONG64 ignored0 : 3;
		ULONG64 PFN : 41; // Physical address of next level table
		ULONG64 ignored1 : 11;
	};
	ULONG64 QuadPart;
} VMX_EPT_ENTRY, *PVMX_EPT_ENTRY;
C_ASSERT(sizeof(VMX_EPT_ENTRY) == 8);

//
// An EPT page-directory-pointer-table entry (PDPTE) that maps a 1 GiB page.
//
typedef union _VMX_EPT_PDPTE {
	struct {
		ULONG64 R : 1; // Read access
		ULONG64 W : 1; // Write access
		ULONG64 X : 1; // Execute access
		ULONG64 MT : 3; // EPT memory type 
		ULONG64 IPAT : 1; // Ignore PAT memory type
		ULONG64 P : 1; //  Must be 1 (otherwise, this entry references an EPT page directory)
		ULONG64 A : 1; // Indicates whether software has accessed the page
		ULONG64 D : 1; // Indicates whether software has written to the page
		ULONG64 ignored0 : 2;
		ULONG64 reserved0 : 18;
		ULONG64 PFN : 22; // Physical address of the page
		ULONG64 ignored1 : 11;
		ULONG64 SVE : 1; // If the “EPT-violation #VE” VM-execution control is 1, EPT 
						 // violations caused by accesses to this page are convertible 
						 // to virtualization exceptions only if this bit is 0
	};
	VMX_EPT_ENTRY Dir; // If the P bit is not set, this entry should be treated as a directory entry
	ULONG64 QuadPart;
} VMX_EPT_PDPTE, *PVMX_EPT_PDPTE;
C_ASSERT(sizeof(VMX_EPT_PDPTE) == 8);

//
// An EPT page-directory entry (PDE) that maps a 2 MiB page.
//
typedef union _VMX_EPT_PDE {
	struct {
		ULONG64 R : 1; // Read access
		ULONG64 W : 1; // Write access
		ULONG64 X : 1; // Execute access
		ULONG64 MT : 3; // EPT memory type 
		ULONG64 IPAT : 1; // Ignore PAT memory type
		ULONG64 P : 1; //  Must be 1 (otherwise, this entry references an EPT page directory)
		ULONG64 A : 1; // Indicates whether software has accessed the page
		ULONG64 D : 1; // Indicates whether software has written to the page
		ULONG64 ignored0 : 2;
		ULONG64 reserved0 : 9;
		ULONG64 PFN : 31; // Physical address of the page
		ULONG64 ignored1 : 11;
		ULONG64 SVE : 1; // If the “EPT-violation #VE” VM-execution control is 1, EPT 
						 // violations caused by accesses to this page are convertible 
						 // to virtualization exceptions only if this bit is 0
	};
	VMX_EPT_ENTRY Dir; // If the P bit is not set, this entry should be treated as a directory entry
	ULONG64 QuadPart;
} VMX_EPT_PDE, *PVMX_EPT_PDE;
C_ASSERT(sizeof(VMX_EPT_PDE) == 8);

//
// An EPT page-table entry (PTE) that maps a 4 KiB page.
//
typedef union _VMX_EPT_PTE {
	struct {
		ULONG64 R : 1; // Read access
		ULONG64 W : 1; // Write access
		ULONG64 X : 1; // Execute access
		ULONG64 MT : 3; // EPT memory type 
		ULONG64 IPAT : 1; // Ignore PAT memory type
		ULONG64 ignored0 : 1; 
		ULONG64 A : 1; // Indicates whether software has accessed the page
		ULONG64 D : 1; // Indicates whether software has written to the page
		ULONG64 ignored1 : 2;
		ULONG64 PFN : 40; // Physical address of the page
		ULONG64 ignored2 : 11;
		ULONG64 SVE : 1; // If the “EPT-violation #VE” VM-execution control is 1, EPT 
						 // violations caused by accesses to this page are convertible 
						 // to virtualization exceptions only if this bit is 0
	};
	ULONG64 QuadPart;
} VMX_EPT_PTE, *PVMX_EPT_PTE;
C_ASSERT(sizeof(VMX_EPT_PTE) == 8);

//
// This is a helper union that makes the logic for calculating
// entry addresses based on guest physical addresses (GPA), EPT
// entries, and host physical addresses (HPA).
//
typedef union _VMX_EPT_ADDRESS {
	//
	// To calculate the address of an EPT entry. We combine bits
	// from the address of the EPT table in question as well as 9
	// bits from the GPA.  The 9 bits selected from the GPA depends
	// on which level of the table we are in (see below).  
	//
	struct {
		ULONG64 reserved1 : 3; // Bits 2:0 are 0
		ULONG64 GPA : 9; // Bits 11:3 are from the GPA
		ULONG64 DIR : 40; // Bits 51:12 are from the directory table address
		ULONG64 reserved2 : 12; // Bits 63:52 are 0;
	};
	
	//
	// This breaks down the GPA into the bits used for the offsets
	// of the given EPT table (see above).
	//
	struct {
		ULONG64 HPA : 12; // Bits 11:0
		ULONG64 PTE : 9; // Bits 20:12
		ULONG64 PDE : 9; // Bits 29:21
		ULONG64 PDPTE : 9; // Bits 38:30
		ULONG64 PML4E : 9; // Bits  47:39
		ULONG64 reserved0 : 16; // Bits 63:48
	};

	//
	// This makes it easy to just assign an EPT entry
	// so we don't have to break out the DIR bits
	// manually.
	//
	PVMX_EPT_ENTRY Entry;

	ULONG64 QuadPart;
} VMX_EPT_ADDRESS, *PVMX_EPT_ADDRESS;
C_ASSERT(sizeof(VMX_EPT_ADDRESS) == 8);