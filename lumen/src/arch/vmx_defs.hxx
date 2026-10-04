#pragma once

#include "../common/types.hxx"

namespace lumen::vmx {

    inline constexpr u32 k_msr_feature_control         = 0x3A;
    inline constexpr u32 k_msr_sysenter_cs             = 0x174;
    inline constexpr u32 k_msr_sysenter_esp            = 0x175;
    inline constexpr u32 k_msr_sysenter_eip            = 0x176;
    inline constexpr u32 k_msr_debugctl                = 0x1D9;
    inline constexpr u32 k_msr_pat                     = 0x277;
    inline constexpr u32 k_msr_efer                    = 0xC0000080;
    inline constexpr u32 k_msr_fs_base                 = 0xC0000100;
    inline constexpr u32 k_msr_gs_base                 = 0xC0000101;

    inline constexpr u32 k_msr_vmx_basic               = 0x480;
    inline constexpr u32 k_msr_vmx_pinbased_ctls       = 0x481;
    inline constexpr u32 k_msr_vmx_procbased_ctls      = 0x482;
    inline constexpr u32 k_msr_vmx_exit_ctls           = 0x483;
    inline constexpr u32 k_msr_vmx_entry_ctls          = 0x484;
    inline constexpr u32 k_msr_vmx_procbased_ctls2     = 0x48B;
    inline constexpr u32 k_msr_vmx_true_pinbased_ctls  = 0x48D;
    inline constexpr u32 k_msr_vmx_true_procbased_ctls = 0x48E;
    inline constexpr u32 k_msr_vmx_true_exit_ctls      = 0x48F;
    inline constexpr u32 k_msr_vmx_true_entry_ctls     = 0x490;
    inline constexpr u32 k_msr_vmx_ept_vpid_cap        = 0x48C;
    inline constexpr u32 k_msr_vmx_cr0_fixed0          = 0x486;
    inline constexpr u32 k_msr_vmx_cr0_fixed1          = 0x487;
    inline constexpr u32 k_msr_vmx_cr4_fixed0          = 0x488;
    inline constexpr u32 k_msr_vmx_cr4_fixed1          = 0x489;

    inline constexpr u64 k_fc_lock              = 1ull << 0;
    inline constexpr u64 k_fc_vmxon_outside_smx = 1ull << 2;
    inline constexpr u64 k_cr4_vmxe             = 1ull << 13;

    inline constexpr u32 k_cpu_based_hlt_exiting      = 1u << 7;
    inline constexpr u32 k_cpu_based_mwait_exiting    = 1u << 10;
    inline constexpr u32 k_cpu_based_use_msr_bitmaps  = 1u << 28;
    inline constexpr u32 k_cpu_based_monitor_exiting  = 1u << 29;
    inline constexpr u32 k_cpu_based_use_secondary    = 1u << 31;

    inline constexpr u32 k_sec_enable_ept         = 1u << 1;
    inline constexpr u32 k_sec_rdtscp             = 1u << 3;
    inline constexpr u32 k_sec_enable_vpid        = 1u << 5;
    inline constexpr u32 k_sec_unrestricted_guest = 1u << 7;
    inline constexpr u32 k_sec_enable_invpcid     = 1u << 12;
    inline constexpr u32 k_sec_enable_xsaves      = 1u << 20;

    inline constexpr u32 k_exit_host_addr_space_size = 1u << 9;
    inline constexpr u32 k_exit_save_ia32_efer       = 1u << 20;
    inline constexpr u32 k_exit_load_ia32_efer       = 1u << 21;

    inline constexpr u32 k_entry_ia32e_mode_guest    = 1u << 9;
    inline constexpr u32 k_entry_load_ia32_efer      = 1u << 15;

    inline constexpr u32 k_vmcs_vpid        = 0x0000;
    inline constexpr u32 k_vmcs_guest_es    = 0x0800;
    inline constexpr u32 k_vmcs_guest_cs    = 0x0802;
    inline constexpr u32 k_vmcs_guest_ss    = 0x0804;
    inline constexpr u32 k_vmcs_guest_ds    = 0x0806;
    inline constexpr u32 k_vmcs_guest_fs    = 0x0808;
    inline constexpr u32 k_vmcs_guest_gs    = 0x080A;
    inline constexpr u32 k_vmcs_guest_ldtr  = 0x080C;
    inline constexpr u32 k_vmcs_guest_tr    = 0x080E;
    inline constexpr u32 k_vmcs_host_es     = 0x0C00;
    inline constexpr u32 k_vmcs_host_cs     = 0x0C02;
    inline constexpr u32 k_vmcs_host_ss     = 0x0C04;
    inline constexpr u32 k_vmcs_host_ds     = 0x0C06;
    inline constexpr u32 k_vmcs_host_fs     = 0x0C08;
    inline constexpr u32 k_vmcs_host_gs     = 0x0C0A;
    inline constexpr u32 k_vmcs_host_tr     = 0x0C0C;

    inline constexpr u32 k_vmcs_io_bitmap_a         = 0x2000;
    inline constexpr u32 k_vmcs_msr_bitmap          = 0x2004;
    inline constexpr u32 k_vmcs_ept_pointer         = 0x201A;
    inline constexpr u32 k_vmcs_link_pointer        = 0x2800;
    inline constexpr u32 k_vmcs_guest_ia32_debugctl = 0x2802;
    inline constexpr u32 k_vmcs_guest_ia32_efer     = 0x2806;
    inline constexpr u32 k_vmcs_host_ia32_efer      = 0x2C02;

    inline constexpr u32 k_vmcs_pin_based_ctls   = 0x4000;
    inline constexpr u32 k_vmcs_proc_based_ctls  = 0x4002;
    inline constexpr u32 k_vmcs_exception_bitmap = 0x4004;
    inline constexpr u32 k_vmcs_vm_exit_ctls     = 0x400C;
    inline constexpr u32 k_vmcs_vm_entry_ctls    = 0x4012;
    inline constexpr u32 k_vmcs_proc_based_ctls2 = 0x401E;

    inline constexpr u32 k_vmcs_vm_exit_reason           = 0x4402;
    inline constexpr u32 k_vmcs_vm_exit_instr_len        = 0x440C;
    inline constexpr u32 k_vmcs_vm_entry_intr_info       = 0x4016;
    inline constexpr u32 k_vmcs_vm_entry_except_err_code = 0x4018;
    inline constexpr u32 k_vmcs_vm_entry_instr_len       = 0x401A;
    inline constexpr u32 k_vmcs_guest_interruptibility   = 0x4824;
    inline constexpr u32 k_vmcs_guest_activity_state     = 0x4826;

    inline constexpr u32 k_vmcs_guest_es_limit   = 0x4800;
    inline constexpr u32 k_vmcs_guest_cs_limit   = 0x4802;
    inline constexpr u32 k_vmcs_guest_ss_limit   = 0x4804;
    inline constexpr u32 k_vmcs_guest_ds_limit   = 0x4806;
    inline constexpr u32 k_vmcs_guest_fs_limit   = 0x4808;
    inline constexpr u32 k_vmcs_guest_gs_limit   = 0x480A;
    inline constexpr u32 k_vmcs_guest_ldtr_limit = 0x480C;
    inline constexpr u32 k_vmcs_guest_tr_limit   = 0x480E;
    inline constexpr u32 k_vmcs_guest_gdtr_limit = 0x4810;
    inline constexpr u32 k_vmcs_guest_idtr_limit = 0x4812;
    inline constexpr u32 k_vmcs_guest_es_ar      = 0x4814;
    inline constexpr u32 k_vmcs_guest_cs_ar      = 0x4816;
    inline constexpr u32 k_vmcs_guest_ss_ar      = 0x4818;
    inline constexpr u32 k_vmcs_guest_ds_ar      = 0x481A;
    inline constexpr u32 k_vmcs_guest_fs_ar      = 0x481C;
    inline constexpr u32 k_vmcs_guest_gs_ar      = 0x481E;
    inline constexpr u32 k_vmcs_guest_ldtr_ar    = 0x4820;
    inline constexpr u32 k_vmcs_guest_tr_ar      = 0x4822;
    inline constexpr u32 k_vmcs_guest_sysenter_cs = 0x482A;
    inline constexpr u32 k_vmcs_host_sysenter_cs  = 0x4C00;

    inline constexpr u32 k_vmcs_cr0_mask        = 0x6000;
    inline constexpr u32 k_vmcs_cr4_mask        = 0x6002;
    inline constexpr u32 k_vmcs_cr0_read_shadow = 0x6004;
    inline constexpr u32 k_vmcs_cr4_read_shadow = 0x6006;

    inline constexpr u32 k_vmcs_guest_cr0       = 0x6800;
    inline constexpr u32 k_vmcs_guest_cr3       = 0x6802;
    inline constexpr u32 k_vmcs_guest_cr4       = 0x6804;
    inline constexpr u32 k_vmcs_guest_es_base   = 0x6806;
    inline constexpr u32 k_vmcs_guest_cs_base   = 0x6808;
    inline constexpr u32 k_vmcs_guest_ss_base   = 0x680A;
    inline constexpr u32 k_vmcs_guest_ds_base   = 0x680C;
    inline constexpr u32 k_vmcs_guest_fs_base   = 0x680E;
    inline constexpr u32 k_vmcs_guest_gs_base   = 0x6810;
    inline constexpr u32 k_vmcs_guest_ldtr_base = 0x6812;
    inline constexpr u32 k_vmcs_guest_tr_base   = 0x6814;
    inline constexpr u32 k_vmcs_guest_gdtr_base = 0x6816;
    inline constexpr u32 k_vmcs_guest_idtr_base = 0x6818;
    inline constexpr u32 k_vmcs_guest_dr7         = 0x681A;
    inline constexpr u32 k_vmcs_guest_rflags      = 0x6820;
    inline constexpr u32 k_vmcs_guest_rsp         = 0x681C;
    inline constexpr u32 k_vmcs_guest_rip         = 0x681E;
    inline constexpr u32 k_vmcs_guest_sysenter_esp = 0x6824;
    inline constexpr u32 k_vmcs_guest_sysenter_eip = 0x6826;

    inline constexpr u32 k_vmcs_host_cr0       = 0x6C00;
    inline constexpr u32 k_vmcs_host_cr3       = 0x6C02;
    inline constexpr u32 k_vmcs_host_cr4       = 0x6C04;
    inline constexpr u32 k_vmcs_host_fs_base   = 0x6C06;
    inline constexpr u32 k_vmcs_host_gs_base   = 0x6C08;
    inline constexpr u32 k_vmcs_host_tr_base   = 0x6C0A;
    inline constexpr u32 k_vmcs_host_gdtr_base = 0x6C0C;
    inline constexpr u32 k_vmcs_host_idtr_base = 0x6C0E;
    inline constexpr u32 k_vmcs_host_sysenter_esp = 0x6C10;
    inline constexpr u32 k_vmcs_host_sysenter_eip = 0x6C12;
    inline constexpr u32 k_vmcs_host_rsp          = 0x6C14;
    inline constexpr u32 k_vmcs_host_rip          = 0x6C16;

    enum e_exit_reason : u32 {
        k_exit_exception_nmi = 0,
        k_exit_cpuid         = 10,
        k_exit_hlt           = 12,
        k_exit_invd          = 13,
        k_exit_mwait         = 36,
        k_exit_monitor       = 39,
        k_exit_pause         = 40,
        k_exit_vmcall        = 18,
        k_exit_vmclear       = 19,
        k_exit_vmlaunch      = 20,
        k_exit_vmptrld       = 21,
        k_exit_vmptrst       = 22,
        k_exit_vmread        = 23,
        k_exit_vmresume      = 24,
        k_exit_vmwrite       = 25,
        k_exit_vmxoff        = 26,
        k_exit_vmxon         = 27,
        k_exit_cr_access     = 28,
        k_exit_rdmsr         = 31,
        k_exit_wrmsr         = 32,
        k_exit_ept_violation = 48,
        k_exit_ept_misconfig = 49,
        k_exit_xsetbv        = 55,
    };

    struct guest_regs_t {
        u64 r15, r14, r13, r12, r11, r10, r9, r8;
        u64 rdi, rsi;
        u64 rsp_dummy, rbp;
        u64 rbx, rdx, rcx, rax;
    };
}