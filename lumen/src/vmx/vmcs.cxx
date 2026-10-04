#include "vmx.hxx"
#include "../arch/intrin.hxx"
#include "../arch/vmx_defs.hxx"

extern "C" void asm_vmexit_entry( );

namespace lumen::vmx {

    namespace {

        u32 fixup_ctls( u64 cap, u32 want ) {
            const u32 allowed0 = static_cast< u32 >( cap );
            const u32 allowed1 = static_cast< u32 >( cap >> 32 );
            u32 v = want;
            v |= allowed0;
            v &= allowed1;
            return v;
        }

        u32 segment_ar( u16 sel ) {
            if ( !sel ) return 0x10000;
            u32 ar = arch::ar_bytes( sel );
            ar = ( ar >> 8 ) & 0xF0FFu;
            return ar;
        }

        u64 segment_base( u16 sel, const arch::desc_ptr_t& gdt ) {
            if ( !sel || ( sel & 0x4 ) ) return 0;
            auto* desc = reinterpret_cast< u8* >( gdt.base + ( sel & ~0x7u ) );
            u64 base = static_cast< u64 >( *reinterpret_cast< u32* >( desc + 2 ) & 0xFFFFFF )
                     | ( static_cast< u64 >( desc[ 7 ] ) << 24 );
            const u8 type = desc[ 5 ] & 0x1F;
            if ( type == 0x02 || type == 0x09 || type == 0x0B ) {
                base |= static_cast< u64 >( *reinterpret_cast< u32* >( desc + 8 ) ) << 32;
            }
            return base;
        }

        u32 seg_limit( u16 sel ) {
            if ( !sel ) return 0;
            return arch::segment_limit( sel );
        }

    }

    bool vmcs_fill( void* host_rsp_top, u64 eptp ) {
        using namespace arch;

        const u64 pin  = rdmsr( k_msr_vmx_true_pinbased_ctls );
        const u64 proc = rdmsr( k_msr_vmx_true_procbased_ctls );
        const u64 exit_ = rdmsr( k_msr_vmx_true_exit_ctls );
        const u64 entry = rdmsr( k_msr_vmx_true_entry_ctls );
        const u64 proc2 = rdmsr( k_msr_vmx_procbased_ctls2 );

        const u32 pin_ctls   = fixup_ctls( pin, 0 );
        // enable MSR bitmap (zeroed = pass through everything, no MSR exits)
        const u32 proc_ctls  = fixup_ctls( proc,
            k_cpu_based_use_secondary |
            k_cpu_based_use_msr_bitmaps |
            k_cpu_based_mwait_exiting |
            k_cpu_based_monitor_exiting );
        const u32 proc_ctls2 = fixup_ctls( proc2,
            k_sec_enable_ept | k_sec_rdtscp | k_sec_enable_invpcid |
            k_sec_unrestricted_guest | k_sec_enable_xsaves );
        const u32 exit_ctls  = fixup_ctls( exit_,
            k_exit_host_addr_space_size | k_exit_save_ia32_efer | k_exit_load_ia32_efer );
        const u32 entry_ctls = fixup_ctls( entry,
            k_entry_ia32e_mode_guest | k_entry_load_ia32_efer );

        vmx_vmwrite( k_vmcs_pin_based_ctls,   pin_ctls );
        vmx_vmwrite( k_vmcs_proc_based_ctls,  proc_ctls );
        vmx_vmwrite( k_vmcs_proc_based_ctls2, proc_ctls2 );
        vmx_vmwrite( k_vmcs_vm_exit_ctls,     exit_ctls );
        vmx_vmwrite( k_vmcs_vm_entry_ctls,    entry_ctls );
        vmx_vmwrite( k_vmcs_exception_bitmap, 0 );

        vmx_vmwrite( k_vmcs_ept_pointer, eptp );
        vmx_vmwrite( k_vmcs_vpid, 1 );
        vmx_vmwrite( k_vmcs_msr_bitmap, global( ).msr_bitmap_pa( ) );

        // explicitly initialize the fields VM-entry checks even though
        // VMCLEAR is supposed to default them.  some microcode is picky.
        vmx_vmwrite( k_vmcs_link_pointer,            ~0ull );
        vmx_vmwrite( k_vmcs_guest_activity_state,    0 );
        vmx_vmwrite( k_vmcs_guest_interruptibility,  0 );
        vmx_vmwrite( k_vmcs_vm_entry_intr_info,      0 );

        // only shadow CR4.VMXE so the guest can't turn VMX off under us.
        vmx_vmwrite( k_vmcs_cr0_mask,        0 );
        vmx_vmwrite( k_vmcs_cr0_read_shadow, 0 );
        vmx_vmwrite( k_vmcs_cr4_mask,        k_cr4_vmxe );
        vmx_vmwrite( k_vmcs_cr4_read_shadow, rcr4( ) & ~k_cr4_vmxe );

        // CR0/CR4 must satisfy IA32_VMX_CRx_FIXED0/FIXED1 or VM-entry
        // fails with "invalid guest state" and the guest triple-faults.
        auto fix_cr0 = []( u64 v ) {
            v |= rdmsr( k_msr_vmx_cr0_fixed0 );
            v &= rdmsr( k_msr_vmx_cr0_fixed1 );
            return v;
        };
        auto fix_cr4 = []( u64 v ) {
            v |= rdmsr( k_msr_vmx_cr4_fixed0 );
            v &= rdmsr( k_msr_vmx_cr4_fixed1 );
            return v;
        };
        const u64 cr0_fixed = fix_cr0( rcr0( ) );
        const u64 cr4_fixed = fix_cr4( rcr4( ) );

        const auto gdt = sgdt( );
        const auto idt = sidt( );

        vmx_vmwrite( k_vmcs_guest_cr0, cr0_fixed );
        vmx_vmwrite( k_vmcs_guest_cr3, rcr3( ) );
        vmx_vmwrite( k_vmcs_guest_cr4, cr4_fixed );
        vmx_vmwrite( k_vmcs_guest_dr7, 0x400 );

        vmx_vmwrite( k_vmcs_guest_sysenter_cs,  rdmsr( k_msr_sysenter_cs ) );
        vmx_vmwrite( k_vmcs_guest_sysenter_esp, rdmsr( k_msr_sysenter_esp ) );
        vmx_vmwrite( k_vmcs_guest_sysenter_eip, rdmsr( k_msr_sysenter_eip ) );

        const u16 cs = read_cs( ), ss = read_ss( ), ds = read_ds( );
        const u16 es = read_es( ), fs = read_fs( ), gs = read_gs( );
        const u16 tr = str( ),     ldtr = sldt( );

        vmx_vmwrite( k_vmcs_guest_cs, cs );
        vmx_vmwrite( k_vmcs_guest_ss, ss );
        vmx_vmwrite( k_vmcs_guest_ds, ds );
        vmx_vmwrite( k_vmcs_guest_es, es );
        vmx_vmwrite( k_vmcs_guest_fs, fs );
        vmx_vmwrite( k_vmcs_guest_gs, gs );
        vmx_vmwrite( k_vmcs_guest_tr,   tr );
        vmx_vmwrite( k_vmcs_guest_ldtr, ldtr );

        vmx_vmwrite( k_vmcs_guest_cs_limit, seg_limit( cs ) );
        vmx_vmwrite( k_vmcs_guest_ss_limit, seg_limit( ss ) );
        vmx_vmwrite( k_vmcs_guest_ds_limit, seg_limit( ds ) );
        vmx_vmwrite( k_vmcs_guest_es_limit, seg_limit( es ) );
        vmx_vmwrite( k_vmcs_guest_fs_limit, seg_limit( fs ) );
        vmx_vmwrite( k_vmcs_guest_gs_limit, seg_limit( gs ) );
        vmx_vmwrite( k_vmcs_guest_tr_limit,   seg_limit( tr ) );
        vmx_vmwrite( k_vmcs_guest_ldtr_limit, seg_limit( ldtr ) );
        vmx_vmwrite( k_vmcs_guest_gdtr_limit, gdt.limit );
        vmx_vmwrite( k_vmcs_guest_idtr_limit, idt.limit );

        vmx_vmwrite( k_vmcs_guest_cs_ar,   segment_ar( cs ) );
        vmx_vmwrite( k_vmcs_guest_ss_ar,   segment_ar( ss ) );
        vmx_vmwrite( k_vmcs_guest_ds_ar,   segment_ar( ds ) );
        vmx_vmwrite( k_vmcs_guest_es_ar,   segment_ar( es ) );
        vmx_vmwrite( k_vmcs_guest_fs_ar,   segment_ar( fs ) );
        vmx_vmwrite( k_vmcs_guest_gs_ar,   segment_ar( gs ) );
        vmx_vmwrite( k_vmcs_guest_tr_ar,   segment_ar( tr ) );
        vmx_vmwrite( k_vmcs_guest_ldtr_ar, segment_ar( ldtr ) );

        vmx_vmwrite( k_vmcs_guest_cs_base,   segment_base( cs, gdt ) );
        vmx_vmwrite( k_vmcs_guest_ss_base,   segment_base( ss, gdt ) );
        vmx_vmwrite( k_vmcs_guest_ds_base,   segment_base( ds, gdt ) );
        vmx_vmwrite( k_vmcs_guest_es_base,   segment_base( es, gdt ) );
        vmx_vmwrite( k_vmcs_guest_fs_base,   rdmsr( k_msr_fs_base ) );
        vmx_vmwrite( k_vmcs_guest_gs_base,   rdmsr( k_msr_gs_base ) );
        vmx_vmwrite( k_vmcs_guest_tr_base,   segment_base( tr, gdt ) );
        vmx_vmwrite( k_vmcs_guest_ldtr_base, segment_base( ldtr, gdt ) );
        vmx_vmwrite( k_vmcs_guest_gdtr_base, gdt.base );
        vmx_vmwrite( k_vmcs_guest_idtr_base, idt.base );

        vmx_vmwrite( k_vmcs_guest_ia32_debugctl, rdmsr( k_msr_debugctl ) );
        vmx_vmwrite( k_vmcs_guest_ia32_efer,     rdmsr( k_msr_efer ) );

        vmx_vmwrite( k_vmcs_host_cr0, cr0_fixed );
        vmx_vmwrite( k_vmcs_host_cr3, rcr3( ) );
        vmx_vmwrite( k_vmcs_host_cr4, cr4_fixed );

        vmx_vmwrite( k_vmcs_host_sysenter_cs,  rdmsr( k_msr_sysenter_cs ) );
        vmx_vmwrite( k_vmcs_host_sysenter_esp, rdmsr( k_msr_sysenter_esp ) );
        vmx_vmwrite( k_vmcs_host_sysenter_eip, rdmsr( k_msr_sysenter_eip ) );

        vmx_vmwrite( k_vmcs_host_cs, cs & 0xF8u );
        vmx_vmwrite( k_vmcs_host_ss, ss & 0xF8u );
        vmx_vmwrite( k_vmcs_host_ds, ds & 0xF8u );
        vmx_vmwrite( k_vmcs_host_es, es & 0xF8u );
        vmx_vmwrite( k_vmcs_host_fs, fs & 0xF8u );
        vmx_vmwrite( k_vmcs_host_gs, gs & 0xF8u );
        vmx_vmwrite( k_vmcs_host_tr, tr & 0xF8u );

        vmx_vmwrite( k_vmcs_host_fs_base,   rdmsr( k_msr_fs_base ) );
        vmx_vmwrite( k_vmcs_host_gs_base,   rdmsr( k_msr_gs_base ) );
        vmx_vmwrite( k_vmcs_host_tr_base,   segment_base( tr, gdt ) );
        vmx_vmwrite( k_vmcs_host_gdtr_base, gdt.base );
        vmx_vmwrite( k_vmcs_host_idtr_base, idt.base );

        vmx_vmwrite( k_vmcs_host_ia32_efer, rdmsr( k_msr_efer ) );

        vmx_vmwrite( k_vmcs_host_rsp, reinterpret_cast< u64 >( host_rsp_top ) );
        vmx_vmwrite( k_vmcs_host_rip, reinterpret_cast< u64 >( &asm_vmexit_entry ) );

        return true;
    }
}