# lumen

> A minimal, modern-C++20 Intel VT-x hypervisor for Windows x64. **Loaded at runtime**. Puts the running OS into a VM, with MTRR-aware EPT and per-process shadow-page hooks.

<p align="center">
  <sub>· VT-x + EPT · MTRR-aware · shadow-page hooks · per-process targeting · modern C++20</sub>
</p>

---

## tl;dr

Load the driver with `sc start`. Every logical processor is placed in VMX non-root operation while Windows continues running normally. Hook arbitrary kernel memory from userland via an IOCtl, reads of the hooked page see the original bytes, execution runs the modified bytes.

```
EPT hook demo

[resolve] KeDelayExecutionThread: ntoskrnl.exe base=0xfffff8037b400000 + rva=0x212d50 = VA 0xfffff8037b612d50

[test 1/3] measuring Sleep(1500) no hook
           took 1515 ms

[hook]    installed @ 0xfffff8037b612d50+3408 (3 bytes)
[list] 1 active hook(s):
  [0] page_pa=0x0000000100612000 mode=exec

[test 2/3] measuring Sleep(1500) with hook
           took 0 ms

[unhook]  removed @ 0xfffff8037b612d50
[list] 0 active hook(s):

[test 3/3] measuring Sleep(1500) after unhook
           took 1516 ms
```

## features

- Intel VT-x hypervisor with EPT — loaded via `sc start`
- **MTRR-aware EPT** builder: parses `IA32_MTRRCAP` / `IA32_MTRR_DEF_TYPE` / fixed + variable MTRRs, hard-floors MMIO regions (LAPIC, IO-APIC, HPET, TPM, PCI MMIO hole, SPI flash) to UC
- **EPT shadow-page hooks** with per-process (`CR3`) targeting — any read sees the original bytes, execution sees the hook
- Cross-CPU `INVEPT` via `KeIpiGenericCall` → VMCALL → `invept` in VMX root
- Clean VMCALL devirtualize path (VMXOFF + manual guest-state restore, no VMRESUME-after-VMXOFF cascade)
- leaves `0x40000000`–`0x400000FF` return zero, `CPUID.1:ECX` bit 31 cleared
- MSR bitmap enabled (every MSR passes through)
- MWAIT/MONITOR trapped

## how the EPT shadow works

For every hooked 4 KiB page we keep two shadow copies

The 4 KiB EPT PTE points at one or the other depending on the last access type. Our `k_exit_ept_violation` handler flips it:

- Guest tries to execute → PTE currently RW → EPT violation (X=0 on fetch) → swap to fake, INVEPT, VMRESUME → guest executes hooked bytes
- Guest reads / writes → PTE currently X-only → EPT violation (R=0 on data) → swap to real, INVEPT, VMRESUME → guest sees original bytes

result: any disassembly tool, integrity check, or `memcmp` sees untouched memory, while execution runs the hook.
With per-process targeting (`target_cr3` set), non-matching processes always get the real page regardless of access type, so the hook only affects the chosen process.

## build

### prerequisites

- Windows 10/11 x64 host, Intel CPU with VT-x + EPT
- Visual Studio 2022 (17.8+) with MSVC v143
- Windows 10/11 SDK + matching WDK 10
- Hyper-V / VBS / HVCI **off**

### build

Open `lumen.sln` in VS 2022, ctrl + b to build. Output is `x64/Release/lumen.sys`.

### enable test signing (reboot once)

```powershell
bcdedit /set testsigning on
bcdedit /set hypervisorlaunchtype off
```

### load

```powershell
sc.exe create lumen type= kernel binpath= "C:\path\to\lumen.sys"
sc.exe start lumen
```

Unload:

```powershell
sc.exe stop lumen
sc.exe delete lumen
```

## usage from your own tool

Open the device and send ioctls — the shared interface is in [`shared/protocol.hxx`](shared/protocol.hxx).

### install a hook (requires admin + \\.\lumen handle)

```c
HANDLE h = CreateFileA("\\\\.\\lumen", GENERIC_READ|GENERIC_WRITE,
                      0, 0, OPEN_EXISTING, 0, 0);

ept_hook_request_t req = {
    .target_va      = kernel_va,         // kernel VA to hook
    .target_cr3     = HOOK_CR3_CURRENT,  // 0 = global; ~0 = current process; else explicit CR3
    .offset_in_page = (unsigned)(kernel_va & 0xFFF),
    .byte_count     = 3,
    .bytes          = { 0x33, 0xC0, 0xC3 }   // xor eax,eax ; ret
};
DWORD got;
DeviceIoControl(h, ioctl_ept_hook, &req, sizeof(req), 0, 0, &got, 0);
```

### unhook / list

```c
ept_unhook_request_t ureq = { .target_va = kernel_va };
DeviceIoControl(h, ioctl_ept_unhook, &ureq, sizeof(ureq), 0, 0, &got, 0);

ept_list_response_t resp;
DeviceIoControl(h, ioctl_ept_list, 0, 0, &resp, sizeof(resp), &got, 0);
```

Resolving a kernel VA from userland is straightforward with `EnumDeviceDrivers` + a `LoadLibrary` of ntoskrnl.exe to look up export offsets.

## project layout

```
lumen/
├── lumen.sln
├── lumen/                        the VS driver project
│   ├── lumen.vcxproj
│   └── src/
│       ├── entry.cxx             DriverEntry
│       ├── arch/
│       │   ├── asm_x64.asm       VM-exit trampoline, VMX helpers, INVEPT
│       │   ├── intrin.hxx        MSVC intrinsic wrappers
│       │   └── vmx_defs.hxx      VMCS / MSR constants from SDM
│       ├── common/
│       │   ├── mem.hxx           pool allocators
│       │   └── types.hxx         u8..u64 + pool tag
│       ├── ioctl/
│       │   ├── device.cxx        \\.\lumen + ioctl dispatch
│       │   └── device.hxx
│       └── vmx/
│           ├── vmx.hxx/.cxx      c_vmx / c_vcpu — VMXON, VMLAUNCH per cpu
│           ├── vmcs.cxx          VMCS field fill
│           ├── ept.hxx/.cxx      EPT
│           └── exits.cxx         VM-exit dispatcher
└── shared/
    └── protocol.hxx              ioctl + hypercall surface
```

## references

- **Intel SDM Vol 3C** — Chapters 24–30 on VMX, EPT, memory typing
- `tandasat/HyperPlatform` — Satoshi Tanda's reference VT-x hypervisor
- `wbenny/hvpp` — Petr Benes' C++17 take
- `SinaKarvandi/Hypervisor-From-Scratch` — the series everyone learns from
- Daniel Pistelli's VT-x blog posts
- The `intel-sdm-vmx` tag on OSDev wiki

## license

MIT — see [LICENSE](LICENSE).