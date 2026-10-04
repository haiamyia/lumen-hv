#pragma once

#include "../common/types.hxx"

namespace lumen::ept {

    inline constexpr u32 k_max_hooks = 32;

    union ept_entry_t {
        u64 raw;
        struct {
            u64 read         : 1;
            u64 write        : 1;
            u64 execute      : 1;
            u64 mem_type     : 3;
            u64 ignore_pat   : 1;
            u64 large        : 1;
            u64 accessed     : 1;
            u64 dirty        : 1;
            u64 user_execute : 1;
            u64 reserved0    : 1;
            u64 pfn          : 36;
            u64 reserved1    : 15;
            u64 suppress_ve  : 1;
        } f;
    };
    static_assert( sizeof( ept_entry_t ) == 8, "ept_entry_t layout" );

    struct hook_t {
        gpa_t        page_pa;      // 4K-aligned GPA of hooked page
        void*        real_page;    // allocated 4K buffer with original bytes
        void*        fake_page;    // allocated 4K buffer with modified bytes
        paddr_t      real_pa;
        paddr_t      fake_pa;
        ept_entry_t* pte;          // the 4K EPT PTE for this GPA
        u64          target_cr3;   // 0 = global ; otherwise only this guest CR3 sees fake
        bool         is_exec;      // true: pte→fake(X=1,R=0) ; false: pte→real(R=1,W=1,X=0)
        bool         in_use;
    };

    struct split_pt_t {
        u64          index_2mb;    // (pdpt_i << 9) | pd_i
        ept_entry_t* pt;           // 512-entry 4K page table (4KB alloc)
        bool         in_use;
    };

    struct c_ept {
        bool    init( );
        void    destroy( );

        u64     eptp( ) const { return m_eptp; }
        paddr_t pml4_pa( ) const;

        // hook API (PASSIVE_LEVEL)
        bool install_hook( gpa_t page_pa, u64 target_cr3,
                           const u8* src_4k, u32 offset,
                           const u8* replacement, u32 count );
        bool remove_hook( gpa_t page_pa );
        u32  list_hooks( hook_t* out, u32 max ) const;

        // VM-exit path (VMX root)
        bool handle_violation( gpa_t gpa, u64 qualification );

    private:
        ept_entry_t* ensure_split( gpa_t gpa );
        ept_entry_t* find_4k_pte( gpa_t gpa );
        hook_t*      find_hook( gpa_t page_pa );
        split_pt_t*  find_split( u64 index_2mb );

        ept_entry_t* m_pml4 = nullptr;
        ept_entry_t* m_pdpt = nullptr;
        ept_entry_t* m_pd   = nullptr;
        u64          m_eptp = 0;

        hook_t       m_hooks[ k_max_hooks ]{ };
        split_pt_t   m_splits[ k_max_hooks ]{ };
    };
}