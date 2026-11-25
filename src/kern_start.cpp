//
//  kern_start.cpp
//  FsckMemsize
//
//  Copyright (c) 2026 seidnerj
//  SPDX-License-Identifier: BSD-3-Clause
//
//  Reports hw.memsize as max(real, 8 GiB) to processes named fsck_hfs, so its
//  auto-picked cache (hw.memsize / 8 for a non-root volume) is at least 1 GiB.
//  Every other process goes to the original handler.
//

#include <Headers/plugin_start.hpp>
#include <Headers/kern_api.hpp>
#include <Headers/kern_mach.hpp>
#include <Headers/kern_patcher.hpp>
#include <Headers/kern_util.hpp>

#include <sys/proc.h>

#include "fsck_memsize.hpp"

// Exported in the IOKit KPI (xnu config/IOKit.exports) but not declared by MacKernelSDK.
extern "C" boolean_t PE_parse_boot_arg_str(const char *arg_string, char *arg_ptr, int size);

using namespace FsckMem;

static uint64_t memsizeFloor = DefaultFloor;
static int (*originalHandler)(struct sysctl_oid *, void *, int, struct sysctl_req *);

static int memsizeHandler(struct sysctl_oid *oidp, void *arg1, int arg2, struct sysctl_req *req) {
	char name[2 * MAXCOMLEN + 1] {};
	proc_selfname(name, sizeof(name));
	uint64_t real = *static_cast<uint64_t *>(arg1);
	uint64_t reported = reportedMemsize(name, real, memsizeFloor);
	if (reported != real) {
		DBGLOG("fsckmem", "reporting hw.memsize %llu instead of %llu to %s", reported, real, name);
		return sysctl_handle_quad(oidp, &reported, 0, req);
	}
	return originalHandler(oidp, arg1, arg2, req);
}

static void patcherLoaded(void *, KernelPatcher &patcher) {
	// hw.memsize is SYSCTL_QUAD in bsd/kern/kern_mib.c; its oid symbol is private, the root list is exported.
	auto root = reinterpret_cast<struct sysctl_oid_list *>(patcher.solveSymbol(KernelPatcher::KernelID, "_sysctl__children"));
	if (root == nullptr) {
		SYSLOG("fsckmem", "cannot resolve _sysctl__children (%d)", patcher.getError());
		patcher.clearError();
		return;
	}

	auto oid = findMemsizeOid<struct sysctl_oid>(root);
	if (oid == nullptr || oid->oid_handler != sysctl_handle_quad) {
		SYSLOG("fsckmem", "hw.memsize not found or not a plain quad, leaving it alone");
		return;
	}

	// The oid lives in __DATA_CONST, which is read-only once the kernel is up.
	if (MachInfo::setKernelWriting(true, KernelPatcher::kernelWriteLock) != KERN_SUCCESS) {
		SYSLOG("fsckmem", "cannot enable kernel writing");
		return;
	}
	originalHandler = oid->oid_handler;
	oid->oid_handler = memsizeHandler;
	MachInfo::setKernelWriting(false, KernelPatcher::kernelWriteLock);

	SYSLOG("fsckmem", "hw.memsize for %s is now max(real %llu, floor %llu)", TargetProcess, *static_cast<uint64_t *>(oid->oid_arg1), memsizeFloor);
}

static void pluginStart() {
	char value[16] {};
	if (PE_parse_boot_arg_str(FloorBootArg, value, sizeof(value))) {
		memsizeFloor = parseFloorArg(value);
		SYSLOG("fsckmem", "%s=%s gives floor %llu", FloorBootArg, value, memsizeFloor);
	}
	lilu.onPatcherLoadForce(patcherLoaded);
}

static const char *bootargOff[] {
	"-fsckmemoff"
};

static const char *bootargDebug[] {
	"-fsckmemdbg"
};

static const char *bootargBeta[] {
	"-fsckmembeta"
};

PluginConfiguration ADDPR(config) {
	xStringify(PRODUCT_NAME),
	parseModuleVersion(xStringify(MODULE_VERSION)),
	LiluAPI::AllowNormal | LiluAPI::AllowInstallerRecovery | LiluAPI::AllowSafeMode,
	bootargOff,
	arrsize(bootargOff),
	bootargDebug,
	arrsize(bootargDebug),
	bootargBeta,
	arrsize(bootargBeta),
	KernelVersion::Sequoia,
	KernelVersion::Sequoia,
	pluginStart
};
