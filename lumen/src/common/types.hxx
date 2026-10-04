#pragma once

#include <ntddk.h>

namespace lumen {

    using u8  = unsigned __int8;
    using u16 = unsigned __int16;
    using u32 = unsigned __int32;
    using u64 = unsigned __int64;

    using i8  = __int8;
    using i16 = __int16;
    using i32 = __int32;
    using i64 = __int64;

    using paddr_t = u64;
    using gpa_t   = u64;
    using gva_t   = u64;

    inline constexpr u64 k_page_size   = 0x1000;
    inline constexpr u64 k_page_mask   = k_page_size - 1;
    inline constexpr u64 k_large_page  = 0x200000;
    inline constexpr u64 k_large_mask  = k_large_page - 1;

    template< typename t_ty >
    constexpr t_ty align_down( t_ty v, u64 a ) {
        return static_cast< t_ty >( ( static_cast< u64 >( v ) ) & ~( a - 1 ) );
    }

    template< typename t_ty >
    constexpr t_ty align_up( t_ty v, u64 a ) {
        return static_cast< t_ty >( ( static_cast< u64 >( v ) + ( a - 1 ) ) & ~( a - 1 ) );
    }

    inline constexpr u32 k_pool_tag = 'enoN';
}