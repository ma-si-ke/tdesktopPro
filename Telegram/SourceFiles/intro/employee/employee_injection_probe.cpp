/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/employee/employee_injection_probe.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "intro/employee/employee_injection_box.h"
#include "base/debug_log.h"
#include "core/application.h"
#include "core/core_settings.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#endif // Q_OS_WIN

namespace Intro::Employee {
namespace {

constexpr auto kStartupDelayMs = crl::time(3 * 1000);
constexpr auto kPeriodMs = crl::time(45 * 1000);
constexpr auto kPrefKey = "employee/injection_probe_level";
// Read 16 bytes so the jump-target decode below can safely read the 8-byte
// absolute address after a `48 B8` (mov rax, imm64) hook. Only compare the
// first 8 for detection — any inline hook overwrites byte 0, so a short
// window suffices and keeps false positives low on stable API prologues.
constexpr auto kReadBytes = 16;
constexpr auto kCompareBytes = 8;

[[nodiscard]] bool BoxOnStrong(ProbeLevel level) {
	return (level == ProbeLevel::Lenient) || (level == ProbeLevel::Strict);
}

[[nodiscard]] bool BoxOnAny(ProbeLevel level) {
	return (level == ProbeLevel::Strict);
}

} // namespace

ProbeLevel InjectionProbeLevel() {
	const auto raw = Core::App().settings().readPref<int>(
		kPrefKey,
		int(ProbeLevel::LogOnly));
	if (raw < int(ProbeLevel::Off) || raw > int(ProbeLevel::Strict)) {
		return ProbeLevel::LogOnly;
	}
	return ProbeLevel(raw);
}

void SetInjectionProbeLevel(ProbeLevel level) {
	Core::App().settings().writePref<int>(kPrefKey, int(level));
}

#ifdef Q_OS_WIN
namespace {

[[nodiscard]] bool IsExecutable(DWORD protect) {
	const auto masked = protect & 0xFF;
	return (masked == PAGE_EXECUTE)
		|| (masked == PAGE_EXECUTE_READ)
		|| (masked == PAGE_EXECUTE_READWRITE)
		|| (masked == PAGE_EXECUTE_WRITECOPY);
}

[[nodiscard]] bool IsWritableExecutable(DWORD protect) {
	const auto masked = protect & 0xFF;
	return (masked == PAGE_EXECUTE_READWRITE)
		|| (masked == PAGE_EXECUTE_WRITECOPY);
}

// Win32 start addresses of every thread in this process. A thread whose
// start lands in private executable memory is a remote/injected thread —
// the signal that distinguishes shellcode from legitimate JIT (Qt/PCRE2
// regex JIT allocates RWX too, but is *called*, never thread-started).
[[nodiscard]] std::vector<uintptr_t> OwnThreadStartAddresses() {
	auto result = std::vector<uintptr_t>();
	using Fn = LONG(NTAPI*)(HANDLE, int, PVOID, ULONG, PULONG);
	static const auto query = reinterpret_cast<Fn>(GetProcAddress(
		GetModuleHandleW(L"ntdll.dll"),
		"NtQueryInformationThread"));
	if (!query) {
		return result;
	}
	const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (snapshot == INVALID_HANDLE_VALUE) {
		return result;
	}
	const auto pid = GetCurrentProcessId();
	auto entry = THREADENTRY32{ sizeof(THREADENTRY32) };
	if (Thread32First(snapshot, &entry)) {
		do {
			if (entry.th32OwnerProcessID != pid) {
				continue;
			}
			const auto thread = OpenThread(
				THREAD_QUERY_INFORMATION,
				FALSE,
				entry.th32ThreadID);
			if (!thread) {
				continue;
			}
			auto start = PVOID(nullptr);
			constexpr auto kThreadQuerySetWin32StartAddress = 9;
			if (!query(
					thread,
					kThreadQuerySetWin32StartAddress,
					&start,
					sizeof(start),
					nullptr)) {
				result.push_back(reinterpret_cast<uintptr_t>(start));
			}
			CloseHandle(thread);
		} while (Thread32Next(snapshot, &entry));
	}
	CloseHandle(snapshot);
	return result;
}

void ScanPrivateExecMemory(std::vector<ProbeFinding> &out) {
	const auto threadStarts = OwnThreadStartAddresses();
	auto info = MEMORY_BASIC_INFORMATION{};
	auto address = uintptr_t(0);
	auto guard = 0;
	while (VirtualQuery(
			reinterpret_cast<LPCVOID>(address),
			&info,
			sizeof(info)) == sizeof(info)) {
		const auto regionSize = SIZE_T(info.RegionSize);
		if (info.State == MEM_COMMIT
			&& info.Type == MEM_PRIVATE
			&& IsExecutable(info.Protect)
			&& !(info.Protect & PAGE_GUARD)) {
			const auto base = reinterpret_cast<uintptr_t>(info.BaseAddress);
			const auto regionEnd = base + regionSize;
			auto injectedThread = false;
			for (const auto start : threadStarts) {
				if (start >= base && start < regionEnd) {
					injectedThread = true;
					break;
				}
			}
			// Dormant private-exec memory (JIT/trampoline pools) is left
			// alone; only a region a thread actually *starts* in counts.
			if (injectedThread) {
				const auto rwx = IsWritableExecutable(info.Protect);
				out.push_back(ProbeFinding{
					.kind = FindingKind::PrivateExecMemory,
					.strong = true,
					.detail = u"privexec base=0x%1 size=%2 protect=0x%3 %4 "
						u"thread-start"_q
						.arg(base, 0, 16)
						.arg(quint64(regionSize))
						.arg(info.Protect, 0, 16)
						.arg(rwx ? u"RWX"_q : u"RX"_q),
				});
			}
		}
		const auto next = address + (regionSize ? regionSize : 0x1000);
		if (next <= address) {
			break;
		}
		address = next;
		if (++guard > 200000) {
			break;
		}
	}
}

// 从磁盘上原始 DLL 文件读取某函数开头的 pristine 字节,用于和内存比对。
[[nodiscard]] bool ReadOnDiskPrologue(
		HMODULE module,
		const void *proc,
		unsigned char *out,
		int count) {
	auto path = std::array<wchar_t, MAX_PATH>{};
	if (!GetModuleFileNameW(module, path.data(), MAX_PATH)) {
		return false;
	}
	const auto file = CreateFileW(
		path.data(),
		GENERIC_READ,
		FILE_SHARE_READ,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return false;
	}
	const auto mapping = CreateFileMappingW(
		file,
		nullptr,
		PAGE_READONLY,
		0,
		0,
		nullptr);
	if (!mapping) {
		CloseHandle(file);
		return false;
	}
	const auto base = static_cast<const unsigned char*>(
		MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
	auto ok = false;
	if (base) {
		const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
		if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
			const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
				base + dos->e_lfanew);
			if (nt->Signature == IMAGE_NT_SIGNATURE) {
				const auto rva = DWORD(static_cast<const unsigned char*>(proc)
					- reinterpret_cast<const unsigned char*>(module));
				auto section = IMAGE_FIRST_SECTION(nt);
				const auto count2 = nt->FileHeader.NumberOfSections;
				for (auto i = 0; i != count2; ++i, ++section) {
					const auto va = section->VirtualAddress;
					const auto vsize = section->Misc.VirtualSize
						? section->Misc.VirtualSize
						: section->SizeOfRawData;
					if (rva >= va && rva < va + vsize) {
						const auto fileOffset = section->PointerToRawData
							+ (rva - va);
						memcpy(out, base + fileOffset, count);
						ok = true;
						break;
					}
				}
			}
		}
		UnmapViewOfFile(base);
	}
	CloseHandle(mapping);
	CloseHandle(file);
	return ok;
}

// 若函数开头是一个跳转指令,解析其目标并判断目标是否落在私有内存。
[[nodiscard]] bool HookTargetIsPrivateMemory(const unsigned char *bytes) {
	auto target = uintptr_t(0);
	if (bytes[0] == 0xE9) {
		const auto rel = *reinterpret_cast<const qint32*>(bytes + 1);
		target = reinterpret_cast<uintptr_t>(bytes) + 5 + rel;
	} else if (bytes[0] == 0xFF && bytes[1] == 0x25) {
		const auto disp = *reinterpret_cast<const qint32*>(bytes + 2);
		const auto slot = reinterpret_cast<uintptr_t>(bytes) + 6 + disp;
		auto probe = MEMORY_BASIC_INFORMATION{};
		if (VirtualQuery(reinterpret_cast<LPCVOID>(slot), &probe, sizeof(probe))
				!= sizeof(probe)
			|| probe.State != MEM_COMMIT) {
			return false;
		}
		target = *reinterpret_cast<const uintptr_t*>(slot);
	} else if (bytes[0] == 0x48 && bytes[1] == 0xB8) {
		target = *reinterpret_cast<const uintptr_t*>(bytes + 2);
	} else {
		return false;
	}
	auto info = MEMORY_BASIC_INFORMATION{};
	if (VirtualQuery(reinterpret_cast<LPCVOID>(target), &info, sizeof(info))
			!= sizeof(info)) {
		return false;
	}
	return (info.State == MEM_COMMIT) && (info.Type == MEM_PRIVATE);
}

struct WatchedApi {
	const wchar_t *module;
	const char *name;
};

void ScanInlineHooks(std::vector<ProbeFinding> &out) {
	static const auto kWatched = std::array<WatchedApi, 9>{ {
		{ L"ntdll.dll", "NtProtectVirtualMemory" },
		{ L"ntdll.dll", "NtWriteVirtualMemory" },
		{ L"ntdll.dll", "NtCreateThreadEx" },
		{ L"ntdll.dll", "NtMapViewOfSection" },
		{ L"ntdll.dll", "LdrLoadDll" },
		{ L"kernelbase.dll", "WriteProcessMemory" },
		{ L"kernel32.dll", "WriteProcessMemory" },
		{ L"user32.dll", "SetClipboardData" },
		{ L"user32.dll", "GetClipboardData" },
	} };
	for (const auto &api : kWatched) {
		const auto module = GetModuleHandleW(api.module);
		if (!module) {
			continue;
		}
		const auto proc = reinterpret_cast<const void*>(
			GetProcAddress(module, api.name));
		if (!proc) {
			continue;
		}
		// Skip forwarded exports (e.g. kernel32!WriteProcessMemory actually
		// lives in kernelbase): if proc is outside this module's image the
		// RVA math below would be meaningless, so verify the bound first.
		const auto moduleBytes = reinterpret_cast<const unsigned char*>(
			module);
		const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
		const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
			moduleBytes + dos->e_lfanew);
		const auto imageSize = nt->OptionalHeader.SizeOfImage;
		const auto procBytes = static_cast<const unsigned char*>(proc);
		if (procBytes < moduleBytes
			|| procBytes >= moduleBytes + imageSize) {
			continue;
		}
		auto memoryBytes = std::array<unsigned char, kReadBytes>{};
		memcpy(memoryBytes.data(), proc, kReadBytes);
		auto diskBytes = std::array<unsigned char, kReadBytes>{};
		if (!ReadOnDiskPrologue(
				module,
				proc,
				diskBytes.data(),
				kReadBytes)) {
			continue;
		}
		if (!memcmp(memoryBytes.data(), diskBytes.data(), kCompareBytes)) {
			continue;
		}
		const auto strong = HookTargetIsPrivateMemory(memoryBytes.data());
		out.push_back(ProbeFinding{
			.kind = FindingKind::InlineHook,
			.strong = strong,
			.detail = u"hook %1!%2 mem=%3 disk=%4 target=%5"_q
				.arg(QString::fromWCharArray(api.module))
				.arg(QString::fromLatin1(api.name))
				.arg(QString::fromLatin1(QByteArray(
					reinterpret_cast<const char*>(memoryBytes.data()),
					kReadBytes).toHex()))
				.arg(QString::fromLatin1(QByteArray(
					reinterpret_cast<const char*>(diskBytes.data()),
					kReadBytes).toHex()))
				.arg(strong ? u"PRIVATE"_q : u"image/unknown"_q),
		});
	}
}

} // namespace

std::vector<ProbeFinding> ScanForInjection() {
	auto result = std::vector<ProbeFinding>();
	ScanPrivateExecMemory(result);
	ScanInlineHooks(result);
	return result;
}
#else // Q_OS_WIN
std::vector<ProbeFinding> ScanForInjection() {
	return {};
}
#endif // Q_OS_WIN

InjectionProbe::InjectionProbe()
: _startupTimer([=] { fire(); })
, _periodicTimer([=] { fire(); }) {
}

void InjectionProbe::startupCheck() {
	_startupTimer.callOnce(kStartupDelayMs);
}

void InjectionProbe::startPeriodic() {
	_periodicTimer.callEach(kPeriodMs);
}

void InjectionProbe::stop() {
	_startupTimer.cancel();
	_periodicTimer.cancel();
}

void InjectionProbe::fire() {
	if (_triggered) {
		return;
	}
	const auto level = InjectionProbeLevel();
	if (level == ProbeLevel::Off) {
		return;
	}
	const auto findings = ScanForInjection();
	if (findings.empty()) {
		return;
	}

	auto strongCount = 0;
	for (const auto &finding : findings) {
		if (finding.strong) {
			++strongCount;
		}
		LOG(("InjectionProbe: %1 [%2] %3"
			).arg(finding.kind == FindingKind::InlineHook
				? u"HOOK"_q
				: u"PRIVEXEC"_q
			).arg(finding.strong ? u"STRONG"_q : u"weak"_q
			).arg(finding.detail));
	}
	LOG(("InjectionProbe: scan done level=%1 findings=%2 strong=%3"
		).arg(int(level)
		).arg(findings.size()
		).arg(strongCount));

	const auto shouldBox = BoxOnAny(level)
		|| (BoxOnStrong(level) && strongCount > 0);
	if (shouldBox && ShowInjectionDetectedBox()) {
		_triggered = true;
		stop();
	}
}

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
