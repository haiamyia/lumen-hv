#pragma once

#include "../common/types.hxx"
#include "ept.hxx"

namespace lumen::vmx {

    inline constexpr u64 k_host_stack_pages = 8;
    inline constexpr u64 k_host_stack_size  = k_host_stack_pages * k_page_size;

    struct c_vcpu {
        bool    init( u32 cpu_index, u64 eptp );
        void    destroy( );

        bool    virtualize( );
        void    devirtualize( );

        u32     index( ) const { return m_index; }
        bool    launched( ) const { return m_launched; }

    private:
        bool    enable_vmx_on_cpu( );
        bool    vmxon( );
        bool    vmcs_configure( );

        u32     m_index    = 0;
        void*   m_vmxon_va = nullptr;
        void*   m_vmcs_va  = nullptr;
        void*   m_host_rsp = nullptr;
        u64     m_eptp     = 0;
        bool    m_launched = false;
    };

    struct c_vmx {
        bool    init( );
        void    destroy( );

        bool    virtualize_all( );
        void    devirtualize_all( );

        u32     vcpu_count( ) const { return m_count; }
        u32     launched_count( ) const;
        u64     ept_pml4_pa( ) const { return m_ept.pml4_pa( ); }
        ept::c_ept& ept( ) { return m_ept; }
        u64     msr_bitmap_pa( ) const { return m_msr_bitmap_pa; }

    private:
        ept::c_ept m_ept{ };
        c_vcpu*    m_vcpus          = nullptr;
        u32        m_count          = 0;
        void*      m_msr_bitmap     = nullptr;
        u64        m_msr_bitmap_pa  = 0;
    };

    c_vmx& global( );
}