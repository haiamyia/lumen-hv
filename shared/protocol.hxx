#pragma once

namespace lumen::protocol {

    inline constexpr wchar_t k_device_name[ ]   = L"\\Device\\lumen";
    inline constexpr wchar_t k_symlink_name[ ]  = L"\\DosDevices\\lumen";
    inline constexpr wchar_t k_usermode_path[ ] = L"\\\\.\\lumen";

    inline constexpr unsigned int k_cpuid_magic = 0x4E4D554C;

    enum class e_hypercall : unsigned long long {
        ping           = 0x0001,
        unload         = 0x0002,
        ept_hook       = 0x0003,
        ept_unhook     = 0x0004,
        invept_broadcast = 0x0005,
    };

    inline constexpr unsigned int make_ioctl( unsigned int fn ) {
        return ( 0x8000u << 16 ) | ( 0u << 14 ) | ( fn << 2 ) | 0u;
    }

    inline constexpr unsigned int ioctl_status     = make_ioctl( 0x800 );
    inline constexpr unsigned int ioctl_virt       = make_ioctl( 0x801 );
    inline constexpr unsigned int ioctl_devirt     = make_ioctl( 0x802 );
    inline constexpr unsigned int ioctl_ept_hook   = make_ioctl( 0x810 );
    inline constexpr unsigned int ioctl_ept_unhook = make_ioctl( 0x811 );
    inline constexpr unsigned int ioctl_ept_list   = make_ioctl( 0x812 );

    inline constexpr unsigned int k_max_hook_bytes = 32;
    inline constexpr unsigned int k_max_hooks      = 32;

    inline constexpr unsigned long long k_hook_cr3_global  = 0;                     // all processes
    inline constexpr unsigned long long k_hook_cr3_current = 0xFFFFFFFFFFFFFFFFULL; // caller's process

    struct ept_hook_request_t {
        unsigned long long target_va;         // kernel VA to hook
        unsigned long long target_cr3;        // 0=global, ~0=current, else explicit CR3
        unsigned int       offset_in_page;    // 0..4095 where to apply bytes
        unsigned int       byte_count;        // 1..k_max_hook_bytes
        unsigned char      bytes[ k_max_hook_bytes ];
    };

    struct ept_unhook_request_t {
        unsigned long long target_va;
    };

    struct ept_list_entry_t {
        unsigned long long page_pa;
        unsigned long long target_cr3;
        unsigned int       is_exec_mode;
        unsigned int       _pad;
    };

    struct ept_list_response_t {
        unsigned int       count;
        unsigned int       _pad;
        ept_list_entry_t   entries[ k_max_hooks ];
    };

    struct status_t {
        unsigned int       version;
        unsigned int       vcpu_count;
        unsigned int       vcpu_virtualized;
        unsigned int       ept_levels;
        unsigned long long ept_pml4_pa;
        char               build_tag[ 32 ];
    };

    inline constexpr unsigned int k_version = 0x00010000;
}
