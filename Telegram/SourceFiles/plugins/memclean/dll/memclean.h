// memclean — standalone memory cleanup DLL
// Core technique derived from Mem Reduct (c) Henry++, GPL-3.0
// https://github.com/henrypp/memreduct
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef _MEMCLEAN_H_
#define _MEMCLEAN_H_

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef MEMCLEAN_EXPORTS
#define MC_API __declspec(dllexport)
#else
#define MC_API __declspec(dllimport)
#endif

// cleanup region mask (same layout as Mem Reduct's REDUCT_* mask)
#define MC_WORKINGSET            0x01 // empty all process working sets
#define MC_SYSTEMFILECACHE       0x02 // shrink system file cache working set
#define MC_STANDBYPRIORITY0LIST  0x04 // purge low-priority standby list (safe)
#define MC_STANDBYLIST           0x08 // purge WHOLE standby list (causes disk re-reads!)
#define MC_MODIFIEDLIST          0x10 // flush modified page list (causes disk writes!)
#define MC_COMBINEMEMORYLISTS    0x20 // combine identical memory pages (win10+)
#define MC_REGISTRYCACHE         0x40 // flush registry hive cache (win8.1+)
#define MC_MODIFIEDFILECACHE     0x80 // flush file buffers of all volumes

// Mem Reduct's defaults: everything except the two "freeze" regions
#define MC_MASK_DEFAULT (MC_WORKINGSET | MC_SYSTEMFILECACHE | MC_STANDBYPRIORITY0LIST | \
	MC_REGISTRYCACHE | MC_COMBINEMEMORYLISTS | MC_MODIFIEDFILECACHE)

#define MC_MASK_ALL 0xFF

typedef struct _MC_RESULT
{
	ULONGLONG freed_bytes;    // physical memory freed (before/after difference, 0 if none)
	ULONG succeeded_mask;     // regions cleaned successfully
	ULONG failed_mask;        // regions that returned an error
	ULONG skipped_mask;       // regions skipped (unsupported on this OS version)
	LONG last_status;         // NTSTATUS of the last failure (0 if none)
} MC_RESULT, *PMC_RESULT;

// TRUE if the current process runs elevated (administrator).
MC_API BOOL WINAPI MC_IsElevated (VOID);

// Enables SeProfileSingleProcessPrivilege + SeIncreaseQuotaPrivilege on the
// process token. Must be called once before MC_Clean. Requires elevation.
// Returns FALSE if either privilege could not be enabled.
MC_API BOOL WINAPI MC_Initialize (VOID);

// Mask of regions supported by the running OS version.
MC_API ULONG WINAPI MC_GetSupportedMask (VOID);

// Cleans the regions in `mask` (use MC_MASK_DEFAULT unless you know better).
// Unsupported regions are skipped, failing regions are recorded and the rest
// still run. `result` is optional. Returns TRUE if at least one region succeeded.
MC_API BOOL WINAPI MC_Clean (ULONG mask, PMC_RESULT result);

// Current physical memory load in percent (0-100).
MC_API ULONG WINAPI MC_GetMemoryPercent (VOID);

#ifdef __cplusplus
}
#endif

#endif // _MEMCLEAN_H_
