#pragma once

#include <ntddk.h>
#include "types.hxx"

inline void* __cdecl operator new( unsigned __int64, void* p ) noexcept { return p; }
inline void  __cdecl operator delete( void*, void* ) noexcept { }

namespace lumen::mem {

    inline void* alloc_contig( u64 bytes ) {
        PHYSICAL_ADDRESS highest = { .QuadPart = static_cast< LONGLONG >( ~0ull ) };
        void* p = MmAllocateContiguousMemory( bytes, highest );
        if ( p ) RtlZeroMemory( p, bytes );
        return p;
    }

    inline void free_contig( void* p ) {
        if ( p ) MmFreeContiguousMemory( p );
    }

    inline void* alloc_npool( u64 bytes ) {
        return ExAllocatePool2( POOL_FLAG_NON_PAGED, bytes, k_pool_tag );
    }

    inline void free_npool( void* p ) {
        if ( p ) ExFreePoolWithTag( p, k_pool_tag );
    }

    inline paddr_t pa_of( void* va ) {
        return static_cast< paddr_t >( MmGetPhysicalAddress( va ).QuadPart );
    }

    inline void* va_of( paddr_t pa ) {
        PHYSICAL_ADDRESS p = { .QuadPart = static_cast< LONGLONG >( pa ) };
        return MmGetVirtualForPhysical( p );
    }
}