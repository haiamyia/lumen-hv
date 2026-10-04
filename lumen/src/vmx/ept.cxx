#include "ept.hxx"
#include "../arch/intrin.hxx"
#include "../common/mem.hxx"

namespace lumen::ept {

    namespace {
        constexpr u64 k_pds_per_pdpt = 512;
        constexpr u64 k_pdes_per_pd  = 512;

        constexpr u8 k_mt_uc = 0;
        constexpr u8 k_mt_wb = 6;

        constexpr u32 k_msr_mtrr_cap          = 0xFE;
        constexpr u32 k_msr_mtrr_def_type     = 0x2FF;
        constexpr u32 k_msr_mtrr_physbase_0   = 0x200;
        constexpr u32 k_msr_mtrr_fix64k_00000 = 0x250;
        constexpr u32 k_msr_mtrr_fix16k_80000 = 0x258;
        constexpr u32 k_msr_mtrr_fix16k_a0000 = 0x259;
        constexpr u32 k_msr_mtrr_fix4k_c0000  = 0x268;
        constexpr u32 k_msr_apic_base         = 0x1B;

        struct mtrr_variable_t {
            u64  base;
            u64  size;
            u8   type;
            bool valid;
        };

        struct mtrr_state_t {
            u8   default_type;
            bool enabled;
            bool fixed_enabled;
            bool fixed_supported;
            u32  variable_count;
            mtrr_variable_t variables[ 16 ];
            u8   fixed[ 88 ];
            u64  apic_base;
        };

        mtrr_state_t g_mtrr{ };

        void read_mtrrs( ) {
            const u64 cap = arch::rdmsr( k_msr_mtrr_cap );
            const u64 def = arch::rdmsr( k_msr_mtrr_def_type );

            g_mtrr.default_type    = static_cast< u8 >( def & 0xFF );
            g_mtrr.enabled         = ( ( def >> 11 ) & 1 ) != 0;
            g_mtrr.fixed_enabled   = ( ( def >> 10 ) & 1 ) != 0;
            g_mtrr.fixed_supported = ( ( cap >> 8  ) & 1 ) != 0;
            g_mtrr.variable_count  = static_cast< u32 >( cap & 0xFF );
            if ( g_mtrr.variable_count > 16 ) g_mtrr.variable_count = 16;

            for ( u32 i = 0; i < g_mtrr.variable_count; ++i ) {
                const u64 base_msr = arch::rdmsr( k_msr_mtrr_physbase_0 + i * 2 );
                const u64 mask_msr = arch::rdmsr( k_msr_mtrr_physbase_0 + i * 2 + 1 );

                g_mtrr.variables[ i ].type  = static_cast< u8 >( base_msr & 0xFF );
                g_mtrr.variables[ i ].base  = base_msr & ~0xFFFull;
                g_mtrr.variables[ i ].valid = ( ( mask_msr >> 11 ) & 1 ) != 0;
                g_mtrr.variables[ i ].size  = 0;

                if ( g_mtrr.variables[ i ].valid ) {
                    const u64 mask = mask_msr & ~0xFFFull;
                    if ( mask != 0 ) {
                        unsigned long idx = 0;
                        _BitScanForward64( &idx, mask );
                        g_mtrr.variables[ i ].size = 1ull << idx;
                    }
                }
            }

            if ( g_mtrr.fixed_supported && g_mtrr.fixed_enabled ) {
                const u64 fix64k = arch::rdmsr( k_msr_mtrr_fix64k_00000 );
                for ( u32 i = 0; i < 8; ++i ) g_mtrr.fixed[ i ] = static_cast< u8 >( ( fix64k >> ( i * 8 ) ) & 0xFF );

                const u64 fix16k_80 = arch::rdmsr( k_msr_mtrr_fix16k_80000 );
                for ( u32 i = 0; i < 8; ++i ) g_mtrr.fixed[ 8 + i ] = static_cast< u8 >( ( fix16k_80 >> ( i * 8 ) ) & 0xFF );

                const u64 fix16k_a0 = arch::rdmsr( k_msr_mtrr_fix16k_a0000 );
                for ( u32 i = 0; i < 8; ++i ) g_mtrr.fixed[ 16 + i ] = static_cast< u8 >( ( fix16k_a0 >> ( i * 8 ) ) & 0xFF );

                for ( u32 n = 0; n < 8; ++n ) {
                    const u64 fix4k = arch::rdmsr( k_msr_mtrr_fix4k_c0000 + n );
                    for ( u32 i = 0; i < 8; ++i ) g_mtrr.fixed[ 24 + n * 8 + i ] = static_cast< u8 >( ( fix4k >> ( i * 8 ) ) & 0xFF );
                }
            }

            g_mtrr.apic_base = arch::rdmsr( k_msr_apic_base ) & 0xFFFFF000ull;
        }

        // known MMIO ranges to UC — bare-metal uncore MCEs if
        // any of these are touched cacheable.
        bool overlaps_known_mmio( u64 base, u64 end ) {
            if ( g_mtrr.apic_base != 0 &&
                 base < g_mtrr.apic_base + 0x1000 && end > g_mtrr.apic_base ) return true;
            if ( base < 0xFEC01000ull && end > 0xFEC00000ull ) return true;
            if ( base < 0xFED01000ull && end > 0xFED00000ull ) return true;
            if ( base < 0xFED45000ull && end > 0xFED40000ull ) return true;
            if ( base < 0x100000000ull && end > 0xFF000000ull ) return true;
            if ( base < 0x100000000ull && end > 0xC0000000ull ) return true;
            if ( base < 0x100000ull ) return true;
            return false;
        }

        u8 resolve_2mb_type( u64 base ) {
            const u64 end = base + 0x200000ull;

            if ( overlaps_known_mmio( base, end ) ) return k_mt_uc;
            if ( !g_mtrr.enabled ) return k_mt_uc;

            u8 result = g_mtrr.default_type;
            bool have_override = false;

            for ( u32 i = 0; i < g_mtrr.variable_count; ++i ) {
                const auto& v = g_mtrr.variables[ i ];
                if ( !v.valid || v.size == 0 ) continue;
                const u64 v_end = v.base + v.size;
                if ( base < v_end && end > v.base ) {
                    if ( v.type == k_mt_uc ) return k_mt_uc;
                    if ( v.type != k_mt_wb ) {
                        result = v.type;
                        have_override = true;
                    } else if ( !have_override ) {
                        result = k_mt_wb;
                    }
                }
            }
            return result;
        }
    }

    bool c_ept::init( ) {
        read_mtrrs( );

        m_pml4 = static_cast< ept_entry_t* >( mem::alloc_contig( k_page_size ) );
        m_pdpt = static_cast< ept_entry_t* >( mem::alloc_contig( k_page_size ) );
        m_pd   = static_cast< ept_entry_t* >( mem::alloc_contig( k_pds_per_pdpt * k_page_size ) );

        if ( !m_pml4 || !m_pdpt || !m_pd ) {
            destroy( );
            return false;
        }

        m_pml4[ 0 ].raw = 0;
        m_pml4[ 0 ].f.read    = 1;
        m_pml4[ 0 ].f.write   = 1;
        m_pml4[ 0 ].f.execute = 1;
        m_pml4[ 0 ].f.pfn     = mem::pa_of( m_pdpt ) >> 12;

        for ( u64 i = 0; i < k_pds_per_pdpt; ++i ) {
            ept_entry_t& e = m_pdpt[ i ];
            e.raw = 0;
            e.f.read    = 1;
            e.f.write   = 1;
            e.f.execute = 1;
            e.f.pfn     = mem::pa_of( &m_pd[ i * k_pdes_per_pd ] ) >> 12;
        }

        for ( u64 pdpt_i = 0; pdpt_i < k_pds_per_pdpt; ++pdpt_i ) {
            for ( u64 pd_i = 0; pd_i < k_pdes_per_pd; ++pd_i ) {
                const u64 pa_2mb = ( pdpt_i << 30 ) | ( pd_i << 21 );
                const u8  mt     = resolve_2mb_type( pa_2mb );

                ept_entry_t& e = m_pd[ pdpt_i * k_pdes_per_pd + pd_i ];
                e.raw = 0;
                e.f.read       = 1;
                e.f.write      = 1;
                e.f.execute    = 1;
                e.f.mem_type   = mt;
                e.f.ignore_pat = 0;
                e.f.large      = 1;
                e.f.pfn        = pa_2mb >> 12;
            }
        }

        // eptp: WB for the paging-structure walk | 4-level walk | PML4 PA
        m_eptp = ( static_cast< u64 >( k_mt_wb ) ) | ( 3ull << 3 ) | mem::pa_of( m_pml4 );
        return true;
    }

    void c_ept::destroy( ) {
        for ( u32 i = 0; i < k_max_hooks; ++i ) {
            if ( m_hooks[ i ].in_use ) {
                if ( m_hooks[ i ].real_page ) mem::free_contig( m_hooks[ i ].real_page );
                if ( m_hooks[ i ].fake_page ) mem::free_contig( m_hooks[ i ].fake_page );
                m_hooks[ i ].in_use = false;
            }
            if ( m_splits[ i ].in_use ) {
                if ( m_splits[ i ].pt ) mem::free_contig( m_splits[ i ].pt );
                m_splits[ i ].in_use = false;
            }
        }

        if ( m_pd )   { mem::free_contig( m_pd );   m_pd   = nullptr; }
        if ( m_pdpt ) { mem::free_contig( m_pdpt ); m_pdpt = nullptr; }
        if ( m_pml4 ) { mem::free_contig( m_pml4 ); m_pml4 = nullptr; }
        m_eptp = 0;
    }

    paddr_t c_ept::pml4_pa( ) const {
        return m_pml4 ? mem::pa_of( m_pml4 ) : 0;
    }

    split_pt_t* c_ept::find_split( u64 index_2mb ) {
        for ( u32 i = 0; i < k_max_hooks; ++i ) {
            if ( m_splits[ i ].in_use && m_splits[ i ].index_2mb == index_2mb )
                return &m_splits[ i ];
        }
        return nullptr;
    }

    ept_entry_t* c_ept::ensure_split( gpa_t gpa ) {
        const u64 pdpt_i = ( gpa >> 30 ) & 0x1FF;
        const u64 pd_i   = ( gpa >> 21 ) & 0x1FF;
        const u64 idx    = ( pdpt_i << 9 ) | pd_i;

        split_pt_t* existing = find_split( idx );
        if ( existing ) return existing->pt;

        split_pt_t* slot = nullptr;
        for ( u32 i = 0; i < k_max_hooks; ++i ) {
            if ( !m_splits[ i ].in_use ) { slot = &m_splits[ i ]; break; }
        }
        if ( !slot ) return nullptr;

        auto* pt = static_cast< ept_entry_t* >( mem::alloc_contig( k_page_size ) );
        if ( !pt ) return nullptr;

        ept_entry_t& pde = m_pd[ pdpt_i * 512 + pd_i ];
        const u8  orig_mt = static_cast< u8 >( pde.f.mem_type );
        const u64 base_pa = ( pdpt_i << 30 ) | ( pd_i << 21 );

        for ( u64 i = 0; i < 512; ++i ) {
            pt[ i ].raw = 0;
            pt[ i ].f.read     = 1;
            pt[ i ].f.write    = 1;
            pt[ i ].f.execute  = 1;
            pt[ i ].f.mem_type = orig_mt;
            pt[ i ].f.pfn      = ( base_pa + i * k_page_size ) >> 12;
        }

        ept_entry_t new_pde{ };
        new_pde.raw = 0;
        new_pde.f.read    = 1;
        new_pde.f.write   = 1;
        new_pde.f.execute = 1;
        new_pde.f.pfn     = mem::pa_of( pt ) >> 12;
        pde.raw = new_pde.raw;

        slot->in_use    = true;
        slot->index_2mb = idx;
        slot->pt        = pt;
        return pt;
    }

    ept_entry_t* c_ept::find_4k_pte( gpa_t gpa ) {
        const u64 pdpt_i = ( gpa >> 30 ) & 0x1FF;
        const u64 pd_i   = ( gpa >> 21 ) & 0x1FF;
        const u64 pt_i   = ( gpa >> 12 ) & 0x1FF;

        ept_entry_t& pde = m_pd[ pdpt_i * 512 + pd_i ];
        if ( pde.f.large ) return nullptr;

        auto* pt = reinterpret_cast< ept_entry_t* >( mem::va_of( pde.f.pfn << 12 ) );
        if ( !pt ) return nullptr;
        return &pt[ pt_i ];
    }

    hook_t* c_ept::find_hook( gpa_t page_pa ) {
        for ( u32 i = 0; i < k_max_hooks; ++i ) {
            if ( m_hooks[ i ].in_use && m_hooks[ i ].page_pa == page_pa )
                return &m_hooks[ i ];
        }
        return nullptr;
    }

    bool c_ept::install_hook( gpa_t target_pa, u64 target_cr3,
                              const u8* src_4k, u32 offset,
                              const u8* replacement, u32 count ) {
        const gpa_t page_pa = target_pa & ~k_page_mask;
        if ( offset + count > k_page_size ) return false;
        if ( find_hook( page_pa ) ) return false;

        hook_t* h = nullptr;
        for ( u32 i = 0; i < k_max_hooks; ++i ) {
            if ( !m_hooks[ i ].in_use ) { h = &m_hooks[ i ]; break; }
        }
        if ( !h ) return false;

        if ( !ensure_split( page_pa ) ) return false;
        ept_entry_t* pte = find_4k_pte( page_pa );
        if ( !pte ) return false;

        void* real = mem::alloc_contig( k_page_size );
        void* fake = mem::alloc_contig( k_page_size );
        if ( !real || !fake ) {
            if ( real ) mem::free_contig( real );
            if ( fake ) mem::free_contig( fake );
            return false;
        }

        RtlCopyMemory( real, src_4k, k_page_size );
        RtlCopyMemory( fake, real, k_page_size );
        auto* fake_bytes = static_cast< u8* >( fake );
        RtlCopyMemory( fake_bytes + offset, replacement, count );

        h->in_use     = true;
        h->page_pa    = page_pa;
        h->real_page  = real;
        h->fake_page  = fake;
        h->real_pa    = mem::pa_of( real );
        h->fake_pa    = mem::pa_of( fake );
        h->pte        = pte;
        h->target_cr3 = target_cr3 & ~0xFFFull;
        h->is_exec    = true;

        ept_entry_t new_pte{ };
        new_pte.raw = 0;
        new_pte.f.execute  = 1;
        new_pte.f.mem_type = pte->f.mem_type;
        new_pte.f.pfn      = h->fake_pa >> 12;
        pte->raw = new_pte.raw;
        return true;
    }

    bool c_ept::remove_hook( gpa_t target_pa ) {
        const gpa_t page_pa = target_pa & ~k_page_mask;
        hook_t* h = find_hook( page_pa );
        if ( !h ) return false;

        ept_entry_t new_pte{ };
        new_pte.raw = 0;
        new_pte.f.read     = 1;
        new_pte.f.write    = 1;
        new_pte.f.execute  = 1;
        new_pte.f.mem_type = h->pte->f.mem_type;
        new_pte.f.pfn      = page_pa >> 12;
        h->pte->raw = new_pte.raw;

        if ( h->real_page ) mem::free_contig( h->real_page );
        if ( h->fake_page ) mem::free_contig( h->fake_page );
        h->in_use    = false;
        h->real_page = nullptr;
        h->fake_page = nullptr;
        h->pte       = nullptr;
        return true;
    }

    u32 c_ept::list_hooks( hook_t* out, u32 max ) const {
        u32 n = 0;
        for ( u32 i = 0; i < k_max_hooks && n < max; ++i ) {
            if ( m_hooks[ i ].in_use ) out[ n++ ] = m_hooks[ i ];
        }
        return n;
    }

    // called from VM-exit handler (VMX root).  qualification bits:
    //   0 = read   1 = write   2 = instruction fetch
    bool c_ept::handle_violation( gpa_t gpa, u64 qualification ) {
        const gpa_t page_pa = gpa & ~k_page_mask;
        hook_t* h = find_hook( page_pa );
        if ( !h ) return false;

        const bool is_fetch = ( qualification & ( 1ull << 2 ) ) != 0;
        const bool is_rw    = ( qualification & 0x3 ) != 0;

        // per-process targeting: non-matching CR3 always sees the real page
        if ( h->target_cr3 != 0 ) {
            constexpr u32 k_vmcs_guest_cr3 = 0x6802;
            const u64 guest_cr3 = arch::vmx_vmread( k_vmcs_guest_cr3 ) & ~0xFFFull;
            if ( guest_cr3 != h->target_cr3 ) {
                if ( h->is_exec ) {
                    ept_entry_t new_pte{ };
                    new_pte.raw = 0;
                    new_pte.f.mem_type = h->pte->f.mem_type;
                    new_pte.f.read    = 1;
                    new_pte.f.write   = 1;
                    new_pte.f.execute = 1;
                    new_pte.f.pfn     = page_pa >> 12;
                    h->pte->raw = new_pte.raw;
                    h->is_exec = false;
                }
                return true;
            }
        }

        ept_entry_t new_pte{ };
        new_pte.raw = 0;
        new_pte.f.mem_type = h->pte->f.mem_type;

        if ( is_fetch && !h->is_exec ) {
            new_pte.f.execute = 1;
            new_pte.f.pfn     = h->fake_pa >> 12;
            h->pte->raw = new_pte.raw;
            h->is_exec = true;
        } else if ( is_rw && h->is_exec ) {
            new_pte.f.read  = 1;
            new_pte.f.write = 1;
            new_pte.f.pfn   = h->real_pa >> 12;
            h->pte->raw = new_pte.raw;
            h->is_exec = false;
        }
        return true;
    }
}