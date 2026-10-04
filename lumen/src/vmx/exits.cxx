#include "vmx.hxx"
#include "ept.hxx"
#include "../arch/intrin.hxx"
#include "../arch/vmx_defs.hxx"
#include "../../../shared/protocol.hxx"

namespace lumen::vmx {

    namespace {

        inline constexpr u32 k_vmcs_exit_qualification     = 0x6400;
        inline constexpr u32 k_vmcs_guest_physical_address = 0x2400;

        void advance_rip( ) {
            const u64 rip = arch::vmx_vmread( k_vmcs_guest_rip );
            const u64 len = arch::vmx_vmread( k_vmcs_vm_exit_instr_len );
            arch::vmx_vmwrite( k_vmcs_guest_rip, rip + len );
        }

        void inject_hw( u32 vector, bool has_error = false, u32 error = 0 ) {
            u64 info = ( vector & 0xFFull ) | ( 3ull << 8 ) | ( 1ull << 31 );
            if ( has_error ) info |= ( 1ull << 11 );
            arch::vmx_vmwrite( k_vmcs_vm_entry_intr_info, info );
            if ( has_error ) arch::vmx_vmwrite( k_vmcs_vm_entry_except_err_code, error );
        }

        void inject_ud( ) { inject_hw( 6 ); }
        void inject_gp( ) { inject_hw( 13, true, 0 ); }

        // #GP in VMX root on the VMM stack cascades into the stackptr family of
        // bugchecks because our stack isnt a thread stack.  reject bad indices.
        bool msr_is_safe( u32 msr ) {
            return msr <= 0x1FFF || ( msr >= 0xC0000000 && msr <= 0xC0001FFF );
        }

        void handle_cpuid( guest_regs_t* r ) {
            const u32 leaf    = static_cast< u32 >( r->rax );
            const u32 subleaf = static_cast< u32 >( r->rcx );

            if ( leaf == protocol::k_cpuid_magic ) {
                r->rax = protocol::k_version;
                r->rbx = 0x6E6D756C;
                r->rcx = global( ).vcpu_count( );
                r->rdx = 0x45505434;
                advance_rip( );
                return;
            }

            // standard hypervisor-probe leaves return zero
            if ( leaf >= 0x40000000 && leaf <= 0x400000FF ) {
                r->rax = 0; r->rbx = 0; r->rcx = 0; r->rdx = 0;
                advance_rip( );
                return;
            }

            const auto c = arch::cpuid( leaf, subleaf );
            r->rax = c.eax;
            r->rbx = c.ebx;
            r->rcx = c.ecx;
            r->rdx = c.edx;
            // CPUID.1:ECX bit 31 = hypervisor-present — clear in case outer HV set it
            if ( leaf == 1 ) r->rcx &= ~( 1ull << 31 );
            advance_rip( );
        }

        void handle_rdmsr( guest_regs_t* r ) {
            const u32 msr = static_cast< u32 >( r->rcx );
            if ( !msr_is_safe( msr ) ) { inject_gp( ); return; }

            u64 v = 0;
            // FS/GS base are swapped on vm-exit — live MSR is host now
            if ( msr == 0xC0000100 )      v = arch::vmx_vmread( k_vmcs_guest_fs_base );
            else if ( msr == 0xC0000101 ) v = arch::vmx_vmread( k_vmcs_guest_gs_base );
            else                          v = arch::rdmsr( msr );
            r->rax = static_cast< u32 >( v );
            r->rdx = static_cast< u32 >( v >> 32 );
            advance_rip( );
        }

        void handle_wrmsr( guest_regs_t* r ) {
            const u32 msr = static_cast< u32 >( r->rcx );
            if ( !msr_is_safe( msr ) || ( msr >= 0x480 && msr <= 0x491 ) ) {
                inject_gp( );
                return;
            }
            const u64 v = ( r->rdx << 32 ) | static_cast< u32 >( r->rax );
            if ( msr == 0xC0000100 )      arch::vmx_vmwrite( k_vmcs_guest_fs_base, v );
            else if ( msr == 0xC0000101 ) arch::vmx_vmwrite( k_vmcs_guest_gs_base, v );
            else                          arch::wrmsr( msr, v );
            advance_rip( );
        }

        void handle_invd( )  { __wbinvd( ); advance_rip( ); }

        void handle_xsetbv( guest_regs_t* r ) {
            const u32 index = static_cast< u32 >( r->rcx );
            const u64 value = ( r->rdx << 32 ) | static_cast< u32 >( r->rax );
            _xsetbv( index, value );
            advance_rip( );
        }

        bool handle_vmcall( guest_regs_t* r ) {
            const auto fn = static_cast< protocol::e_hypercall >( r->rcx );
            switch ( fn ) {
            case protocol::e_hypercall::ping:
                r->rax = protocol::k_version;
                advance_rip( );
                return true;

            case protocol::e_hypercall::invept_broadcast: {
                arch::invept_single_context( global( ).ept( ).eptp( ) );
                advance_rip( );
                return true;
            }

            case protocol::e_hypercall::unload: {
                const u64 rip = arch::vmx_vmread( k_vmcs_guest_rip )
                              + arch::vmx_vmread( k_vmcs_vm_exit_instr_len );
                const u64 rsp = arch::vmx_vmread( k_vmcs_guest_rsp );
                const u64 fl  = arch::vmx_vmread( k_vmcs_guest_rflags );
                const u64 cr3 = arch::vmx_vmread( k_vmcs_guest_cr3 );

                arch::vmx_off( );
                arch::wcr4( arch::rcr4( ) & ~k_cr4_vmxe );

                // asm_devirt_path reads these back on pop and uses them to
                // transition out of VMX root into the guest
                r->rax = rip;
                r->r8  = rsp;
                r->rcx = fl;
                r->rdx = cr3;
                return false;
            }

            default:
                inject_ud( );
                return true;
            }
        }

    }

    // returns 0 for normal exit (asm trampoline VMRESUMEs), 1 for unload
    extern "C" unsigned int cxx_vmexit_handler( guest_regs_t* r ) {
        const u32 reason = static_cast< u32 >( arch::vmx_vmread( k_vmcs_vm_exit_reason ) ) & 0xFFFF;

        switch ( reason ) {
        case k_exit_cpuid:         handle_cpuid( r );   break;
        case k_exit_rdmsr:         handle_rdmsr( r );   break;
        case k_exit_wrmsr:         handle_wrmsr( r );   break;
        case k_exit_invd:          handle_invd( );      break;
        case k_exit_xsetbv:        handle_xsetbv( r );  break;

        case k_exit_mwait:
        case k_exit_monitor:
        case k_exit_pause:
            advance_rip( );
            break;

        case k_exit_ept_violation: {
            const u64 gpa  = arch::vmx_vmread( k_vmcs_guest_physical_address );
            const u64 qual = arch::vmx_vmread( k_vmcs_exit_qualification );
            if ( global( ).ept( ).handle_violation( gpa, qual ) ) {
                arch::invept_single_context( global( ).ept( ).eptp( ) );
                // guest retries the access with new EPT mapping — don't advance RIP
            } else {
                const u64 len = arch::vmx_vmread( k_vmcs_vm_exit_instr_len );
                if ( len ) {
                    const u64 rip = arch::vmx_vmread( k_vmcs_guest_rip );
                    arch::vmx_vmwrite( k_vmcs_guest_rip, rip + len );
                }
            }
            break;
        }

        case k_exit_vmcall:
            if ( !handle_vmcall( r ) ) return 1;
            break;

        case k_exit_vmclear:
        case k_exit_vmlaunch:
        case k_exit_vmptrld:
        case k_exit_vmptrst:
        case k_exit_vmread:
        case k_exit_vmresume:
        case k_exit_vmwrite:
        case k_exit_vmxoff:
        case k_exit_vmxon:
            inject_ud( );
            break;

        default: {
            const u64 len = arch::vmx_vmread( k_vmcs_vm_exit_instr_len );
            if ( len ) {
                const u64 rip = arch::vmx_vmread( k_vmcs_guest_rip );
                arch::vmx_vmwrite( k_vmcs_guest_rip, rip + len );
            }
            break;
        }
        }

        return 0;
    }
}