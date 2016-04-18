/*++

Copyright (c) Joe T. Sylve.  All rights reserved.

Header Name:

	debug.h

Abstract:

	This header defines macros that help with debugging.

Author:

	Joe T. Sylve (@jtsylve) 18-Apr-2016 - Initial version

Environment:

	Kernel mode only.

--*/

#pragma once

//
// Print a formatted message to the kernel debugger.
//
#define SHV_PRINT(...) DbgPrintEx(77, 0, __VA_ARGS__)

//
// Print a formatted message to the kernel debugger or
// does nothing if not a debug build.
//
#define SHV_DEBUG_PRINT(...) IF_DEBUG { DbgPrintEx(77, 0, __VA_ARGS__); }

//
// If a kernel debugger is attached, cause a breakpoint.
//
#define SHV_DEBUG_BREAKPOINT() { if (KD_DEBUGGER_NOT_PRESENT == FALSE) KdBreakPoint(); }

//
// If a kernel debugger is attached, cause a breakpoint after printing a message.
//
#define SHV_DEBUG_BREAKPOINT_MESSAGE(...) { DbgPrintEx(77, 0, __VA_ARGS__); SHV_DEBUG_BREAKPOINT(); }