#include "vmx.hxx"
#include "../arch/intrin.hxx"
#include "../arch/vmx_defs.hxx"
#include "../common/mem.hxx"
#include "../../../shared/protocol.hxx"

extern "C" void asm_vmexit_entry( );
extern "C" unsigned char asm_capture_and_launch( CONTEXT* );
extern "C" NTSYSAPI VOID NTAPI RtlCaptureContext( PCONTEXT );

namespace lumen::vmx {

    namespace {
        c_vmx g_vmx{ };

        u32 vmx_revision_id( ) {
            return static_cast< u32 >( arch::rdmsr( k_msr_vmx_basic ) ) & 0x7FFF'FFFFu;
        }
    }

    c_vmx& global( ) { return g_vmx; }

    bool c_vmx::init( ) {
        m_count = KeQueryActiveProcessorCount( nullptr );
        if ( !m_count ) return false;

        m_vcpus = static_cast< c_vcpu* >( mem::alloc_npool( sizeof( c_vcpu ) * m_count ) );
        if ( !m_vcpus ) return false;

        // MSR bitmap: 4 KiB zero-filled = pass through every MSR access.
        m_msr_bitmap = mem::alloc_contig( k_page_size );
        if ( !m_msr_bitmap ) return false;
        m_msr_bitmap_pa = mem::pa_of( m_msr_bitmap );

        if ( !m_ept.init( ) ) return false;

        for ( u32 i = 0; i < m_count; ++i ) {
            if ( !m_vcpus[ i ].init( i, m_ept.eptp( ) ) ) return false;
        }
        return true;
    }

    void c_vmx::destroy( ) {
        if ( m_vcpus ) {
            for ( u32 i = 0; i < m_count; ++i ) m_vcpus[ i ].destroy( );
            mem::free_npool( m_vcpus );
            m_vcpus = nullptr;
        }
        if ( m_msr_bitmap ) {
            mem::free_contig( m_msr_bitmap );
            m_msr_bitmap    = nullptr;
            m_msr_bitmap_pa = 0;
        }
        m_ept.destroy( );
        m_count = 0;
    }

    bool c_vmx::virtualize_all( ) {
        for ( u32 i = 0; i < m_count; ++i ) {
            PROCESSOR_NUMBER pn{ };
            KeGetProcessorNumberFromIndex( i, &pn );

            GROUP_AFFINITY ga{ };
            ga.Group = pn.Group;
            ga.Mask  = static_cast< KAFFINITY >( 1ull ) << pn.Number;

            GROUP_AFFINITY old{ };
            KeSetSystemGroupAffinityThread( &ga, &old );
            m_vcpus[ i ].virtualize( );
            KeRevertToUserGroupAffinityThread( &old );
        }
        return launched_count( ) == m_count;
    }

    void c_vmx::devirtualize_all( ) {
        for ( u32 i = 0; i < m_count; ++i ) {
            PROCESSOR_NUMBER pn{ };
            KeGetProcessorNumberFromIndex( i, &pn );

            GROUP_AFFINITY ga{ };
            ga.Group = pn.Group;
            ga.Mask  = static_cast< KAFFINITY >( 1ull ) << pn.Number;

            GROUP_AFFINITY old{ };
            KeSetSystemGroupAffinityThread( &ga, &old );
            m_vcpus[ i ].devirtualize( );
            KeRevertToUserGroupAffinityThread( &old );
        }
    }

    u32 c_vmx::launched_count( ) const {
        u32 n = 0;
        for ( u32 i = 0; i < m_count; ++i ) if ( m_vcpus[ i ].launched( ) ) ++n;
        return n;
    }

    bool c_vcpu::init( u32 cpu_index, u64 eptp ) {
        m_index = cpu_index;
        m_eptp  = eptp;

        m_vmxon_va = mem::alloc_contig( k_page_size );
        m_vmcs_va  = mem::alloc_contig( k_page_size );
        m_host_rsp = mem::alloc_npool( k_host_stack_size );

        if ( !m_vmxon_va || !m_vmcs_va || !m_host_rsp ) return false;

        const u32 rev = vmx_revision_id( );
        *static_cast< u32* >( m_vmxon_va ) = rev;
        *static_cast< u32* >( m_vmcs_va )  = rev;
        return true;
    }

    void c_vcpu::destroy( ) {
        if ( m_vmxon_va ) { mem::free_contig( m_vmxon_va ); m_vmxon_va = nullptr; }
        if ( m_vmcs_va )  { mem::free_contig( m_vmcs_va );  m_vmcs_va  = nullptr; }
        if ( m_host_rsp ) { mem::free_npool( m_host_rsp );  m_host_rsp = nullptr; }
        m_launched = false;
    }

    bool c_vcpu::enable_vmx_on_cpu( ) {
        u64 fc = arch::rdmsr( k_msr_feature_control );
        if ( !( fc & k_fc_lock ) ) {
            fc |= k_fc_lock | k_fc_vmxon_outside_smx;
            arch::wrmsr( k_msr_feature_control, fc );
            fc = arch::rdmsr( k_msr_feature_control );
        }
        if ( !( fc & k_fc_vmxon_outside_smx ) ) return false;
        arch::wcr4( arch::rcr4( ) | k_cr4_vmxe );
        return true;
    }

    bool c_vcpu::vmxon( ) {
        u64 pa = mem::pa_of( m_vmxon_va );
        return arch::vmx_on( &pa ) == 0;
    }

    bool vmcs_fill( void* host_rsp_top, u64 eptp );

    bool c_vcpu::vmcs_configure( ) {
        u64 pa = mem::pa_of( m_vmcs_va );
        if ( arch::vmx_vmclear( &pa ) != 0 ) return false;
        if ( arch::vmx_vmptrld( &pa ) != 0 ) return false;

        auto top = reinterpret_cast< u8* >( m_host_rsp ) + k_host_stack_size - 0x10;
        return vmcs_fill( top, m_eptp );
    }

    bool c_vcpu::virtualize( ) {
        if ( !enable_vmx_on_cpu( ) ) return false;
        if ( !vmxon( ) ) {
            arch::wcr4( arch::rcr4( ) & ~k_cr4_vmxe );
            return false;
        }
        if ( !vmcs_configure( ) ) {
            arch::vmx_off( );
            arch::wcr4( arch::rcr4( ) & ~k_cr4_vmxe );
            return false;
        }

        CONTEXT* ctx = static_cast< CONTEXT* >( mem::alloc_npool( sizeof( CONTEXT ) ) );
        if ( !ctx ) {
            arch::vmx_off( );
            arch::wcr4( arch::rcr4( ) & ~k_cr4_vmxe );
            return false;
        }
        ctx->ContextFlags = CONTEXT_FULL;
        RtlCaptureContext( ctx );

        // on re-entry as guest, m_launched is already true -> we fall through
        if ( !m_launched ) {
            m_launched = true;
            unsigned char r = asm_capture_and_launch( ctx );
            m_launched = false;
            mem::free_npool( ctx );
            arch::vmx_off( );
            arch::wcr4( arch::rcr4( ) & ~k_cr4_vmxe );
            ( void )r;
            return false;
        }

        mem::free_npool( ctx );
        return true;
    }

    void c_vcpu::devirtualize( ) {
        if ( !m_launched ) return;
        arch::vmcall( static_cast< u64 >( protocol::e_hypercall::unload ) );
        m_launched = false;
    }
}