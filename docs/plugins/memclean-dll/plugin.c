// memclean — plugin ABI layer
// Core technique derived from Mem Reduct (c) Henry++, GPL-3.0
// https://github.com/henrypp/memreduct
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Everything crosses the boundary as UTF-8 JSON. The host owns the
// output buffer: we only fill it, or say how much room we would need.

#define MEMCLEAN_EXPORTS
#include "memclean.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PLUGIN_API __declspec(dllexport)
#define PLUGIN_ABI_VERSION 1

// Mem Reduct's defaults, without the two regions that freeze the system.
#define CLEAN_MASK MC_MASK_DEFAULT

#define MODE_INTERVAL 0
#define MODE_THRESHOLD 1

// Right after a clean the load may still sit above the threshold, so
// the automatic path keeps a cooldown of its own and never spins.
#define AUTO_COOLDOWN_MS 60000

static BOOL _initialized = FALSE;
static BOOL _initialize_failed = FALSE;
static ULONGLONG _last_auto_clean = 0;

//
// Tiny JSON reading. The host is the only caller and always sends the
// same shape, so looking a key up inside "settings" is enough.
//

static const char *_json_find (const char *json, const char *key)
{
	char pattern[64];
	const char *found;

	if (!json)
		return NULL;

	_snprintf_s (pattern, sizeof (pattern), _TRUNCATE, "\"%s\"", key);

	found = strstr (json, pattern);

	if (!found)
		return NULL;

	found += strlen (pattern);

	while (*found == ' ' || *found == ':' || *found == '\t')
		found += 1;

	return found;
}

static BOOL _json_bool (const char *json, const char *key, BOOL fallback)
{
	const char *value = _json_find (json, key);

	if (!value)
		return fallback;

	if (strncmp (value, "true", 4) == 0)
		return TRUE;

	if (strncmp (value, "false", 5) == 0)
		return FALSE;

	return fallback;
}

static LONG _json_int (const char *json, const char *key, LONG fallback)
{
	const char *value = _json_find (json, key);
	LONG result;

	if (!value || (*value != '-' && (*value < '0' || *value > '9')))
		return fallback;

	result = strtol (value, NULL, 10);

	return result;
}

//
// Answering
//

// Returns the bytes written, or minus the size we would need.
static int _write_answer (char *out, int capacity, const char *text)
{
	const size_t length = strlen (text);

	if (!out || capacity < 0 || (size_t)capacity < length)
		return -(int)length;

	memcpy (out, text, length);

	return (int)length;
}

static int _write_error (char *out, int capacity, const char *message)
{
	char buffer[512];

	_snprintf_s (buffer, sizeof (buffer), _TRUNCATE,
		"{\"error\":\"%s\"}", message);

	return _write_answer (out, capacity, buffer);
}

static int _write_status (char *out, int capacity, const char *toast)
{
	char buffer[512];

	// The label bound to "statusLine" is refreshed by every answer, so
	// the current load stays visible while the page is open.
	if (toast)
	{
		_snprintf_s (buffer, sizeof (buffer), _TRUNCATE,
			"{\"toast\":\"%s\",\"text\":{\"statusLine\":\"当前内存占用：%lu%%\"}}",
			toast, MC_GetMemoryPercent ());
	}
	else
	{
		_snprintf_s (buffer, sizeof (buffer), _TRUNCATE,
			"{\"text\":{\"statusLine\":\"当前内存占用：%lu%%\"}}",
			MC_GetMemoryPercent ());
	}

	return _write_answer (out, capacity, buffer);
}

//
// Cleaning
//

static BOOL _ensure_initialized (VOID)
{
	if (_initialize_failed)
		return FALSE;

	if (_initialized)
		return TRUE;

	if (!MC_Initialize ())
	{
		_initialize_failed = TRUE;

		return FALSE;
	}

	_initialized = TRUE;

	return TRUE;
}

static int _do_clean (char *out, int capacity)
{
	MC_RESULT result = {0};
	char toast[256];
	double megabytes;

	if (!_ensure_initialized ())
		return _write_error (out, capacity, "内存清理初始化失败，功能不可用。");

	if (!MC_Clean (CLEAN_MASK, &result))
		return _write_error (out, capacity, "内存清理失败。");

	megabytes = (double)result.freed_bytes / (1024.0 * 1024.0);

	_snprintf_s (toast, sizeof (toast), _TRUNCATE,
		"内存清理完成，释放了 %.1f MB。", megabytes);

	return _write_status (out, capacity, toast);
}

// The host only calls this on the declared interval, deciding whether
// anything is due is our job.
static int _do_tick (const char *args, char *out, int capacity)
{
	const ULONGLONG now = GetTickCount64 ();
	const BOOL automatic = _json_bool (args, "auto", FALSE);
	const LONG mode = _json_int (args, "mode", MODE_INTERVAL);
	const LONG interval = _json_int (args, "interval", 300);
	const LONG threshold = _json_int (args, "threshold", 80);
	ULONGLONG passed;

	if (!automatic)
		return _write_status (out, capacity, NULL);

	passed = now - _last_auto_clean;

	if (mode == MODE_INTERVAL)
	{
		if (_last_auto_clean && passed < (ULONGLONG)interval * 1000)
			return _write_status (out, capacity, NULL);
	}
	else
	{
		if (_last_auto_clean && passed < AUTO_COOLDOWN_MS)
			return _write_status (out, capacity, NULL);

		if ((LONG)MC_GetMemoryPercent () < threshold)
			return _write_status (out, capacity, NULL);
	}

	_last_auto_clean = now;

	return _do_clean (out, capacity);
}

//
// Plugin ABI
//

PLUGIN_API int __stdcall Plugin_Abi (VOID)
{
	return PLUGIN_ABI_VERSION;
}

PLUGIN_API int __stdcall Plugin_Init (const char *config, char *out, int capacity)
{
	(VOID)config;

	// Cleaning needs privileges only an elevated process can enable, so
	// this is where the host learns to grey the interface out.
	if (!MC_IsElevated ())
	{
		return _write_answer (out, capacity,
			"{\"available\":false,"
			"\"reason\":\"需要以管理员身份运行本程序才能清理内存。\"}");
	}

	return _write_answer (out, capacity, "{\"available\":true}");
}

PLUGIN_API int __stdcall Plugin_Call (const char *action, const char *args, char *out, int capacity)
{
	if (!action)
		return _write_error (out, capacity, "缺少动作名。");

	if (strcmp (action, "clean") == 0)
		return _do_clean (out, capacity);

	if (strcmp (action, "tick") == 0)
		return _do_tick (args, out, capacity);

	if (strcmp (action, "status") == 0)
		return _write_status (out, capacity, NULL);

	return _write_error (out, capacity, "未知的动作。");
}

PLUGIN_API void __stdcall Plugin_Shutdown (VOID)
{
	_initialized = FALSE;
	_initialize_failed = FALSE;
	_last_auto_clean = 0;
}
