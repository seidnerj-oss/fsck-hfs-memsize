//
//  fsck_memsize.hpp
//  FsckMemsize
//
//  Copyright (c) 2026 seidnerj
//  SPDX-License-Identifier: BSD-3-Clause
//
//  Kernel-independent logic, shared by the kext and the host unit test.
//

#ifndef FSCK_MEMSIZE_HPP
#define FSCK_MEMSIZE_HPP

#include <stdint.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/sysctl.h>

namespace FsckMem {

constexpr uint64_t GiB = 1024ULL * 1024 * 1024;
constexpr uint64_t DefaultFloor = 8 * GiB;
constexpr uint64_t MaxFloorGiB = 1024;
constexpr const char *TargetProcess = "fsck_hfs";
constexpr const char *FloorBootArg = "fsckmemfloor";

// bsd/sys/sysctl.h: CTL_HW = 6, HW_MEMSIZE = 24.
constexpr int OidHw = 6;
constexpr int OidHwMemsize = 24;

// The hw.memsize value to report: max(real, floor) to fsck_hfs, the real value to everyone else.
inline uint64_t reportedMemsize(const char *processName, uint64_t realMemsize, uint64_t floor) {
	if (processName == nullptr || strcmp(processName, TargetProcess) != 0)
		return realMemsize;
	return realMemsize > floor ? realMemsize : floor;
}

// Parse the fsckmemfloor boot-arg value: a decimal GiB count in 1..MaxFloorGiB.
// Returns DefaultFloor for a missing, empty, non-decimal or out-of-range value.
inline uint64_t parseFloorArg(const char *value) {
	if (value == nullptr || *value == '\0')
		return DefaultFloor;
	uint64_t gib = 0;
	for (const char *p = value; *p != '\0'; p++) {
		if (*p < '0' || *p > '9')
			return DefaultFloor;
		gib = gib * 10 + static_cast<uint64_t>(*p - '0');
		if (gib > MaxFloorGiB)
			return DefaultFloor;
	}
	return gib == 0 ? DefaultFloor : gib * GiB;
}

// Templates so the host test can use a mirror of the kernel's struct sysctl_oid,
// which the userspace SDK does not define.
template <typename Oid, typename List>
inline Oid *findOid(List *list, int number, const char *name) {
	Oid *oid;
	SLIST_FOREACH(oid, list, oid_link) {
		if (oid->oid_number == number && oid->oid_name != nullptr && strcmp(oid->oid_name, name) == 0)
			return oid;
	}
	return nullptr;
}

// Locate hw.memsize under the sysctl root list, or return nullptr.
template <typename Oid, typename List>
inline Oid *findMemsizeOid(List *root) {
	auto hw = findOid<Oid>(root, OidHw, "hw");
	if (hw == nullptr || (hw->oid_kind & CTLTYPE) != CTLTYPE_NODE || hw->oid_arg1 == nullptr)
		return nullptr;
	auto oid = findOid<Oid>(static_cast<List *>(hw->oid_arg1), OidHwMemsize, "memsize");
	if (oid == nullptr || (oid->oid_kind & CTLTYPE) != CTLTYPE_QUAD || oid->oid_arg1 == nullptr)
		return nullptr;
	return oid;
}

} // namespace FsckMem

#endif // FSCK_MEMSIZE_HPP
