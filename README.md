# FsckMemsize

A [Lilu](https://github.com/acidanthera/Lilu) plugin for macOS Sequoia (Darwin 24, x86_64) that reports `hw.memsize` as max(real memory, 8 GiB) to processes named `fsck_hfs`. Every other process sees the real value, and machines with more than 8 GiB are unaffected.

It exists to get a 4 GB Intel Mac (tested target: MacBook5,1 via OpenCore Legacy Patcher) through the Sequoia installer when the installer's own `fsck_hfs` run fails on an HFS+ volume that is actually fine.

## Why

`fsck_hfs` from hfs-683.120.3 (Sequoia) auto-sizes its block cache from physical memory (`lib_fsck_hfs/check.c`, around line 324):

```c
rv = sysctlbyname("hw.memsize", &memSize, &dsize, NULL, 0);
...
int d = (state.hotroot && !state.lflag) ? 2 : 8;
state.reqCacheSize = memSize / d;
```

For a volume that is not the booted root, that is `hw.memsize / 8`: 512 MB on a 4 GB machine. On some large HFS+ volumes the extended-attributes B-tree check (`BTCheckUnusedNodes`, `lib_fsck_hfs/dfalib/SVerify2.c`) then reports errors that disappear with a bigger cache: `fsck_hfs -fn -c 1g /dev/rdiskXsY` passes on the same disk. The macOS installer runs `fsck_hfs` itself, with no way to pass `-c`, and stops on the failure.

With this kext loaded, `fsck_hfs` on a machine with less than 8 GiB reads 8 GiB and picks a 1 GiB cache on its own.

## How it works

`hw.memsize` is a `SYSCTL_QUAD` backed by `max_mem_actual` (xnu `bsd/kern/kern_mib.c`, tag xnu-11417.140.69). Its oid symbol is private, so the plugin:

1. resolves the exported sysctl root list `_sysctl__children` through Lilu's `KernelPatcher`,
2. walks to `hw` (`CTL_HW`) and then `memsize` (`HW_MEMSIZE`), refusing to continue unless the oid is a quad whose handler is `sysctl_handle_quad`,
3. swaps that one oid's handler (with kernel write protection lifted briefly via Lilu) for a wrapper that answers max(real, floor) when `proc_selfname()` is `fsck_hfs`, and calls the original handler for everyone else. The floor is 8 GiB unless the `fsckmemfloor` boot-arg overrides it.

Nothing else in the kernel is changed.

## Build

Requires Xcode (any recent version; builds on Apple Silicon for x86_64).

```sh
make vendor   # clones Lilu 1.7.1 and MacKernelSDK at pinned commits into vendor/
make test     # host-side unit test of the matching logic
make          # build/Release/FsckMemsize.kext and build/Debug/FsckMemsize.kext
```

## Install (OpenCore)

1. Copy `FsckMemsize.kext` to `EFI/OC/Kexts/`.
2. Add a `Kernel -> Add` entry **after** `Lilu.kext`:

   | Key | Value |
   |---|---|
   | Arch | `x86_64` |
   | BundlePath | `FsckMemsize.kext` |
   | Enabled | `true` |
   | ExecutablePath | `Contents/MacOS/FsckMemsize` |
   | MinKernel | `24.0.0` |
   | MaxKernel | `24.99.99` |
   | PlistPath | `Contents/Info.plist` |

3. Run `ocvalidate` from your OpenCore release on the edited `config.plist`.

The kext loads in normal, safe and installer/recovery boots. Boot arguments:

- `-fsckmemoff` disables it.
- `-fsckmemdbg` enables debug logging with the Debug build.
- `fsckmemfloor=<GiB>` (testing only) replaces the 8 GiB floor with a decimal value from 1 to 1024; anything else is ignored. Use it to see the effect on a machine that already has 8 GiB or more, e.g. `fsckmemfloor=64`.

## Check it works

From Terminal in the installer or recovery (booted through OpenCore):

```sh
sysctl hw.memsize                          # still the real value, e.g. 4294967296
fsck_hfs -fn -d /dev/rdiskXsY 2>&1 | grep cacheSize   # cacheSize=1048576K (1 GiB)
log show --last 10m | grep fsckmem                     # install and floor lines
```

## Upgrade notes: MacBook5,1, El Capitan to Sequoia in place

What else went wrong on the upgrade this kext was written for, in case it helps someone on the same path.

1. **The installer's fsck_hfs fails falsely on less than 8 GB of RAM** (`-69845`, "File system verify or repair failed", before the APFS conversion). That is what this kext fixes. Reported to Apple as FB25715239.
2. **fsck alone could not fix pre-existing catalog damage.** About 18,000 missing thread records; repeated `fsck_hfs -fyr` passes from both hfs-683 and hfs-753 never converged. A DiskWarrior 5 catalog rebuild fixed it.
3. **That DiskWarrior rebuild caused two new problems.**
   - It cleared the volume's journaled flag, so the installer refused the disk ("This disk is not formatted as Mac OS Extended (Journaled)") even though a journal existed. `diskutil disableJournal diskXsY` then `diskutil enableJournal diskXsY` fixed it.
   - It moved `/private`, `/Users` and the local directory database into `Rescued Items` as "Missing Folder N". The installer still converted the disk to APFS, but every first boot crashed and fell back to Recovery. Identifying each folder by its contents, moving it back, filling the rest from `/System/Library/Templates/Data` (`cp -Rpn`, no overwrite) and restoring owner, mode and flags from that template fixed it. If you have ever run DiskWarrior, look for `Rescued Items` at the volume root before upgrading.
4. **The staged upgrade lacked `x86legacyap.im4m`**, so with `Misc -> Security -> SecureBootModel` set, the macOS Installer stage rebooted back to OS X Utilities. Setting it to `Disabled` on the USB fixed it.
5. **The firmware boot picker (Option) hung on a gray screen with a cursor** while a USB hub and an Ethernet cable were attached. An NVRAM reset (Cmd-Option-P-R until the second chime) and unplugging both made `EFI Boot` appear again.
6. **Recovery on this model has no Wi-Fi and no sshd.** A direct Ethernet cable to another Mac works (both sides take a 169.254.x.x address). On the other Mac run `nc -l <its-169.254-address> 4444`; in Recovery's Terminal run `bash -i >& /dev/tcp/<that-address>/4444 0>&1` for a remote root shell. It ends at the next reboot.

### Versions

| Component | Version |
|---|---|
| Mac | MacBook5,1 (late 2008 aluminum), 4 GB RAM, 250 GB HDD, HFS+ |
| From / to | OS X El Capitan 10.11.6 to macOS Sequoia 15.8.3 (installer app 20.8.12) |
| OpenCore Legacy Patcher | 2.5.1 (GUI), with the OpenCore it bundles; config checked with ocvalidate 1.0.4 |
| Lilu | 1.7.1 |
| Other kexts on the USB | RestrictEvents 1.1.7, AppleALC 1.6.3, CryptexFixup 1.0.4, AMFIPass 1.4.1, BlueToolFixup 2.6.9, CSLVFixup 2.6.1, IO80211ElCap 11.1, nForceEthernet 2.0.22 |
| FsckMemsize | 1.0.0, built with Xcode 27.1 (27A9275), Apple clang 21.0.0, MacKernelSDK 3f750085, against xnu-11417.140.69 |
| fsck_hfs that failed | hfs-683.120.3.706.5 (Sequoia installer) |
| fsck_hfs that passed | hfs-753.41.1 (macOS 27), and El Capitan's own |
| Apple source compared | hfs-683.120.3 and hfs-715.120.4 (same RAM-based cache sizing) |
| DiskWarrior | 5.0, run from the Sequoia installer's Terminal |

## Risks

- **Root-volume checks.** For the booted root volume `fsck_hfs` uses `hw.memsize / 2`, so it would ask for 4 GiB (capped at its 3 GiB maximum) on a 4 GB machine. That cannot happen in the installer, where the target is not the root, but it could during a manual single-user `fsck_hfs` of an HFS+ root. Remove the kext, or boot with `-fsckmemoff`, once the upgrade is done.
- **Memory pressure.** A 1 GiB cache on a 4 GB machine is what `-c 1g` does by hand; it works on the original target but leaves less headroom.
- **Kernel memory write.** The handler swap writes one pointer in `__DATA_CONST` with write protection lifted for that instant, the same mechanism Lilu uses for its own patches.
- **Untested combinations.** Only Sequoia (Darwin 24) is enabled. Other kernels are refused by Lilu's version check.

## License

BSD 3-Clause, the same license as Lilu. See [LICENSE](LICENSE).

The built kext also contains Lilu's `plugin_start.cpp` (BSD 3-Clause) and links MacKernelSDK's `libkmod.a` (Apple Public Source License 2.0). Their notices are in [THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES) and ship in every release zip.
