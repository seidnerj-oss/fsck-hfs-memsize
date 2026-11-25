// Copyright (c) 2026 seidnerj
// SPDX-License-Identifier: BSD-3-Clause
// Host-side tests for src/fsck_memsize.hpp. Build and run with `make test`.
#include <stdio.h>
#include <stdlib.h>

#include "../src/fsck_memsize.hpp"

using namespace FsckMem;

// Mirror of the kernel's struct sysctl_oid (bsd/sys/sysctl.h), absent from the userspace SDK.
struct sysctl_req;
struct test_oid;
SLIST_HEAD(test_oid_list, test_oid);
struct test_oid {
	struct test_oid_list *oid_parent;
	SLIST_ENTRY(test_oid) oid_link;
	int oid_number;
	int oid_kind;
	void *oid_arg1;
	int oid_arg2;
	const char *oid_name;
	int (*oid_handler)(struct test_oid *, void *, int, struct sysctl_req *);
	const char *oid_fmt;
	const char *oid_descr;
	int oid_version;
	int oid_refcnt;
};

static int failures;

#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static void testReportedMemsize() {
	// fsck_hfs below the floor gets the floor.
	CHECK(reportedMemsize("fsck_hfs", 4 * GiB, DefaultFloor) == 8 * GiB);
	CHECK(reportedMemsize("fsck_hfs", 8 * GiB - 1, DefaultFloor) == 8 * GiB);
	// fsck_hfs at or above the floor gets its real memory.
	CHECK(reportedMemsize("fsck_hfs", 8 * GiB, DefaultFloor) == 8 * GiB);
	CHECK(reportedMemsize("fsck_hfs", 16 * GiB, DefaultFloor) == 16 * GiB);
	// A raised floor (test boot-arg) lifts machines that have more than 8 GiB.
	CHECK(reportedMemsize("fsck_hfs", 16 * GiB, 32 * GiB) == 32 * GiB);
	// Every other process sees the real value, whatever the floor.
	CHECK(reportedMemsize("fsck_apfs", 4 * GiB, DefaultFloor) == 4 * GiB);
	CHECK(reportedMemsize("fsck_hfs2", 4 * GiB, DefaultFloor) == 4 * GiB);
	CHECK(reportedMemsize("fsck_hf", 4 * GiB, DefaultFloor) == 4 * GiB);
	CHECK(reportedMemsize("FSCK_HFS", 4 * GiB, DefaultFloor) == 4 * GiB);
	CHECK(reportedMemsize("", 4 * GiB, DefaultFloor) == 4 * GiB);
	CHECK(reportedMemsize(nullptr, 4 * GiB, DefaultFloor) == 4 * GiB);
	CHECK(reportedMemsize("sysctl", 16 * GiB, 32 * GiB) == 16 * GiB);
	// fsck_hfs divides by 8 for a non-root volume: the default floor gives the 1 GiB cache that passes.
	CHECK(DefaultFloor / 8 == 1 * GiB);
}

static void testParseFloorArg() {
	CHECK(parseFloorArg("32") == 32 * GiB);
	CHECK(parseFloorArg("1") == 1 * GiB);
	CHECK(parseFloorArg("1024") == 1024 * GiB);
	CHECK(parseFloorArg("0008") == 8 * GiB);
	// Missing, empty, zero, out of range or non-decimal: keep the default.
	CHECK(parseFloorArg(nullptr) == DefaultFloor);
	CHECK(parseFloorArg("") == DefaultFloor);
	CHECK(parseFloorArg("0") == DefaultFloor);
	CHECK(parseFloorArg("1025") == DefaultFloor);
	CHECK(parseFloorArg("99999999999999999999999") == DefaultFloor);
	CHECK(parseFloorArg("-8") == DefaultFloor);
	CHECK(parseFloorArg("16g") == DefaultFloor);
	CHECK(parseFloorArg("0x20") == DefaultFloor);
	CHECK(parseFloorArg("x") == DefaultFloor);
	CHECK(parseFloorArg(" 8") == DefaultFloor);
}

static int dummyHandler(struct test_oid *, void *, int, struct sysctl_req *) {
	return 0;
}

static struct test_oid makeOid(int number, int kind, void *arg1, const char *name) {
	struct test_oid oid {};
	oid.oid_number = number;
	oid.oid_kind = kind;
	oid.oid_arg1 = arg1;
	oid.oid_name = name;
	oid.oid_handler = dummyHandler;
	return oid;
}

static void testFindMemsizeOid() {
	uint64_t memsize = 4 * GiB;
	struct test_oid_list root = SLIST_HEAD_INITIALIZER(root);
	struct test_oid_list hwChildren = SLIST_HEAD_INITIALIZER(hwChildren);

	auto kern = makeOid(1, CTLTYPE_NODE, nullptr, "kern");
	auto hw = makeOid(OidHw, CTLTYPE_NODE, &hwChildren, "hw");
	auto ncpu = makeOid(3, CTLTYPE_INT, nullptr, "ncpu");
	auto usable = makeOid(OidHwMemsize, CTLTYPE_QUAD, &memsize, "memsize_usable");

	CHECK(findMemsizeOid<test_oid>(&root) == nullptr);

	SLIST_INSERT_HEAD(&root, &kern, oid_link);
	CHECK(findMemsizeOid<test_oid>(&root) == nullptr);

	SLIST_INSERT_HEAD(&root, &hw, oid_link);
	SLIST_INSERT_HEAD(&hwChildren, &ncpu, oid_link);
	SLIST_INSERT_HEAD(&hwChildren, &usable, oid_link);
	// Right number, wrong name: not ours.
	CHECK(findMemsizeOid<test_oid>(&root) == nullptr);

	auto wrongType = makeOid(OidHwMemsize, CTLTYPE_INT, &memsize, "memsize");
	SLIST_INSERT_HEAD(&hwChildren, &wrongType, oid_link);
	CHECK(findMemsizeOid<test_oid>(&root) == nullptr);
	SLIST_REMOVE_HEAD(&hwChildren, oid_link);

	auto memsizeOid = makeOid(OidHwMemsize, CTLTYPE_QUAD, &memsize, "memsize");
	SLIST_INSERT_HEAD(&hwChildren, &memsizeOid, oid_link);
	CHECK(findMemsizeOid<test_oid>(&root) == &memsizeOid);

	// A hw entry that is not a node is rejected.
	hw.oid_kind = CTLTYPE_INT;
	CHECK(findMemsizeOid<test_oid>(&root) == nullptr);
}

int main() {
	testReportedMemsize();
	testParseFloorArg();
	testFindMemsizeOid();
	if (failures != 0) {
		fprintf(stderr, "%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}
	printf("all tests passed\n");
	return EXIT_SUCCESS;
}
