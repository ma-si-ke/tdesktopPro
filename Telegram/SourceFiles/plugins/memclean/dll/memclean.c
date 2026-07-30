// memclean — standalone memory cleanup DLL
// Core technique derived from Mem Reduct (c) Henry++, GPL-3.0
// https://github.com/henrypp/memreduct
//
// SPDX-License-Identifier: GPL-3.0-or-later

#define MEMCLEAN_EXPORTS
#include "memclean.h"

#pragma comment (lib, "ntdll.lib")
#pragma comment (lib, "advapi32.lib")

//
// Undocumented NT definitions (from phnt / Mem Reduct)
//

typedef LONG NTSTATUS;

#define NT_SUCCESS(status) (((NTSTATUS)(status)) >= 0)

#define SystemMemoryListInformation 80
#define SystemFileCacheInformationEx 81
#define SystemCombinePhysicalMemoryInformation 130
#define SystemRegistryReconciliationInformation 155

#define SE_INCREASE_QUOTA_PRIVILEGE 5
#define SE_PROF_SINGLE_PROCESS_PRIVILEGE 13

typedef enum _SYSTEM_MEMORY_LIST_COMMAND
{
	MemoryCaptureAccessedBits,
	MemoryCaptureAndResetAccessedBits,
	MemoryEmptyWorkingSets,
	MemoryFlushModifiedList,
	MemoryPurgeStandbyList,
	MemoryPurgeLowPriorityStandbyList
} SYSTEM_MEMORY_LIST_COMMAND;

typedef struct _SYSTEM_FILECACHE_INFORMATION
{
	SIZE_T CurrentSize;
	SIZE_T PeakSize;
	ULONG PageFaultCount;
	SIZE_T MinimumWorkingSet;
	SIZE_T MaximumWorkingSet;
	SIZE_T CurrentSizeIncludingTransitionInPages;
	SIZE_T PeakSizeIncludingTransitionInPages;
	ULONG TransitionRePurposeCount;
	ULONG Flags;
} SYSTEM_FILECACHE_INFORMATION;

typedef struct _MEMORY_COMBINE_INFORMATION_EX
{
	HANDLE Handle;
	ULONG_PTR PagesCombined;
	ULONG Flags;
} MEMORY_COMBINE_INFORMATION_EX;

NTSYSAPI NTSTATUS NTAPI NtSetSystemInformation (INT system_information_class, PVOID system_information, ULONG system_information_length);
NTSYSAPI NTSTATUS NTAPI RtlAdjustPrivilege (ULONG privilege, BOOLEAN enable, BOOLEAN client, PBOOLEAN was_enabled);
NTSYSAPI NTSTATUS NTAPI RtlGetVersion (PRTL_OSVERSIONINFOW version_info);

//
// Helpers
//

static BOOL _mc_isosversiongreaterorequal (ULONG major, ULONG minor)
{
	RTL_OSVERSIONINFOW osvi = {0};

	osvi.dwOSVersionInfoSize = sizeof (osvi);

	if (!NT_SUCCESS (RtlGetVersion (&osvi)))
		return FALSE;

	if (osvi.dwMajorVersion != major)
		return osvi.dwMajorVersion > major;

	return osvi.dwMinorVersion >= minor;
}

static ULONGLONG _mc_getavailablememory (VOID)
{
	MEMORYSTATUSEX msex = {0};

	msex.dwLength = sizeof (msex);

	if (!GlobalMemoryStatusEx (&msex))
		return 0;

	return msex.ullAvailPhys;
}

// Flush file buffers of every mounted volume. Equivalent to Mem Reduct's
// _app_flushvolumecache, but via documented volume enumeration APIs instead
// of raw MountManager IOCTLs.
static BOOL _mc_flushvolumes (VOID)
{
	WCHAR volume_name[MAX_PATH];
	HANDLE hfind, hvolume;
	SIZE_T length;
	BOOL is_success = TRUE;

	hfind = FindFirstVolumeW (volume_name, RTL_NUMBER_OF (volume_name));

	if (hfind == INVALID_HANDLE_VALUE)
		return FALSE;

	do
	{
		// "\\?\Volume{...}\" -> "\\?\Volume{...}" (no trailing slash to open the volume itself)
		length = wcslen (volume_name);

		if (length && volume_name[length - 1] == L'\\')
			volume_name[length - 1] = UNICODE_NULL;

		hvolume = CreateFileW (
			volume_name,
			GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL,
			OPEN_EXISTING,
			0,
			NULL
		);

		if (hvolume != INVALID_HANDLE_VALUE)
		{
			if (!FlushFileBuffers (hvolume))
				is_success = FALSE;

			CloseHandle (hvolume);
		}
	}
	while (FindNextVolumeW (hfind, volume_name, RTL_NUMBER_OF (volume_name)));

	FindVolumeClose (hfind);

	return is_success;
}

static NTSTATUS _mc_memorylistcommand (SYSTEM_MEMORY_LIST_COMMAND command)
{
	return NtSetSystemInformation (SystemMemoryListInformation, &command, sizeof (command));
}

//
// Public API
//

MC_API BOOL WINAPI MC_IsElevated (VOID)
{
	TOKEN_ELEVATION elevation = {0};
	HANDLE htoken;
	ULONG returned_length;
	BOOL is_elevated = FALSE;

	if (OpenProcessToken (GetCurrentProcess (), TOKEN_QUERY, &htoken))
	{
		if (GetTokenInformation (htoken, TokenElevation, &elevation, sizeof (elevation), &returned_length))
			is_elevated = !!elevation.TokenIsElevated;

		CloseHandle (htoken);
	}

	return is_elevated;
}

MC_API BOOL WINAPI MC_Initialize (VOID)
{
	BOOLEAN was_enabled;
	NTSTATUS status1, status2;

	status1 = RtlAdjustPrivilege (SE_PROF_SINGLE_PROCESS_PRIVILEGE, TRUE, FALSE, &was_enabled);
	status2 = RtlAdjustPrivilege (SE_INCREASE_QUOTA_PRIVILEGE, TRUE, FALSE, &was_enabled);

	return NT_SUCCESS (status1) && NT_SUCCESS (status2);
}

MC_API ULONG WINAPI MC_GetSupportedMask (VOID)
{
	ULONG mask = MC_MASK_ALL;

	if (!_mc_isosversiongreaterorequal (6, 3)) // win8.1
		mask &= ~MC_REGISTRYCACHE;

	if (!_mc_isosversiongreaterorequal (10, 0)) // win10
		mask &= ~MC_COMBINEMEMORYLISTS;

	return mask;
}

MC_API BOOL WINAPI MC_Clean (ULONG mask, PMC_RESULT result)
{
	MC_RESULT local_result = {0};
	ULONGLONG available_before, available_after;
	ULONG supported_mask;
	NTSTATUS status;

	supported_mask = MC_GetSupportedMask ();

	local_result.skipped_mask = mask & ~supported_mask;

	mask &= supported_mask;

	available_before = _mc_getavailablememory ();

	if (mask & MC_WORKINGSET)
	{
		status = _mc_memorylistcommand (MemoryEmptyWorkingSets);

		if (NT_SUCCESS (status))
		{
			local_result.succeeded_mask |= MC_WORKINGSET;
		}
		else
		{
			local_result.failed_mask |= MC_WORKINGSET;
			local_result.last_status = status;
		}
	}

	if (mask & MC_SYSTEMFILECACHE)
	{
		SYSTEM_FILECACHE_INFORMATION sfci = {0};

		sfci.MinimumWorkingSet = MAXSIZE_T;
		sfci.MaximumWorkingSet = MAXSIZE_T;

		status = NtSetSystemInformation (SystemFileCacheInformationEx, &sfci, sizeof (sfci));

		if (NT_SUCCESS (status))
		{
			local_result.succeeded_mask |= MC_SYSTEMFILECACHE;
		}
		else
		{
			local_result.failed_mask |= MC_SYSTEMFILECACHE;
			local_result.last_status = status;
		}
	}

	if (mask & MC_MODIFIEDFILECACHE)
	{
		if (_mc_flushvolumes ())
		{
			local_result.succeeded_mask |= MC_MODIFIEDFILECACHE;
		}
		else
		{
			local_result.failed_mask |= MC_MODIFIEDFILECACHE;
		}
	}

	if (mask & MC_MODIFIEDLIST)
	{
		status = _mc_memorylistcommand (MemoryFlushModifiedList);

		if (NT_SUCCESS (status))
		{
			local_result.succeeded_mask |= MC_MODIFIEDLIST;
		}
		else
		{
			local_result.failed_mask |= MC_MODIFIEDLIST;
			local_result.last_status = status;
		}
	}

	if (mask & MC_STANDBYLIST)
	{
		status = _mc_memorylistcommand (MemoryPurgeStandbyList);

		if (NT_SUCCESS (status))
		{
			local_result.succeeded_mask |= MC_STANDBYLIST;
		}
		else
		{
			local_result.failed_mask |= MC_STANDBYLIST;
			local_result.last_status = status;
		}
	}

	if (mask & MC_STANDBYPRIORITY0LIST)
	{
		status = _mc_memorylistcommand (MemoryPurgeLowPriorityStandbyList);

		if (NT_SUCCESS (status))
		{
			local_result.succeeded_mask |= MC_STANDBYPRIORITY0LIST;
		}
		else
		{
			local_result.failed_mask |= MC_STANDBYPRIORITY0LIST;
			local_result.last_status = status;
		}
	}

	if (mask & MC_REGISTRYCACHE)
	{
		status = NtSetSystemInformation (SystemRegistryReconciliationInformation, NULL, 0);

		if (NT_SUCCESS (status))
		{
			local_result.succeeded_mask |= MC_REGISTRYCACHE;
		}
		else
		{
			local_result.failed_mask |= MC_REGISTRYCACHE;
			local_result.last_status = status;
		}
	}

	if (mask & MC_COMBINEMEMORYLISTS)
	{
		MEMORY_COMBINE_INFORMATION_EX combine_info = {0};

		status = NtSetSystemInformation (SystemCombinePhysicalMemoryInformation, &combine_info, sizeof (combine_info));

		if (NT_SUCCESS (status))
		{
			local_result.succeeded_mask |= MC_COMBINEMEMORYLISTS;
		}
		else
		{
			local_result.failed_mask |= MC_COMBINEMEMORYLISTS;
			local_result.last_status = status;
		}
	}

	available_after = _mc_getavailablememory ();

	if (available_after > available_before)
		local_result.freed_bytes = available_after - available_before;

	if (result)
		*result = local_result;

	return local_result.succeeded_mask != 0;
}

MC_API ULONG WINAPI MC_GetMemoryPercent (VOID)
{
	MEMORYSTATUSEX msex = {0};

	msex.dwLength = sizeof (msex);

	if (!GlobalMemoryStatusEx (&msex))
		return 0;

	return msex.dwMemoryLoad;
}
