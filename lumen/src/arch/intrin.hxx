#pragma once

#include "../common/types.hxx"
#include <intrin.h>

extern "C" {
    unsigned __int64 asm_read_cs( );
    unsigned __int64 asm_read_ss( );
    unsigned __int64 asm_read_ds( );
    unsigned __int64 asm_read_es( );
    unsigned __int64 asm_read_fs( );
    unsigned __int64 asm_read_gs( );
    unsigned __int64 asm_str( );
    unsigned __int64 asm_sldt( );
    void             asm_sgdt( void* );
    void             asm_sidt( void* );
    unsigned __int64 asm_lar( unsigned __int64 );
    unsigned __int64 asm_lsl( unsigned __int64 );
    unsigned __int64 asm_vmcall( unsigned __int64, unsigned __int64, unsigned __int64 );
    unsigned __int64 asm_invept( unsigned __int64 type, void* desc );
}

namespace lumen::arch {

    struct cpuid_t { u32 eax, ebx, ecx, edx; };

    inline cpuid_t cpuid( u32 leaf, u32 subleaf = 0 ) {
        int regs[ 4 ] = { };
        __cpuidex( regs, static_cast< int >( leaf ), static_cast< int >( subleaf ) );
        return { static_cast< u32 >( regs[ 0 ] ),
                 static_cast< u32 >( regs[ 1 ] ),
                 static_cast< u32 >( regs[ 2 ] ),
                 static_cast< u32 >( regs[ 3 ] ) };
    }

    inline u64 rdmsr( u32 msr )           { return __readmsr( msr ); }
    inline void wrmsr( u32 msr, u64 v )   { __writemsr( msr, v ); }

    inline u64 rcr0( ) { return __readcr0( ); }
    inline u64 rcr3( ) { return __readcr3( ); }
    inline u64 rcr4( ) { return __readcr4( ); }
    inline void wcr0( u64 v ) { __writecr0( v ); }
    inline void wcr4( u64 v ) { __writecr4( v ); }

    inline u64 rflags( ) { return __readeflags( ); }

    inline u16 read_cs( ) { return static_cast< u16 >( asm_read_cs( ) ); }
    inline u16 read_ss( ) { return static_cast< u16 >( asm_read_ss( ) ); }
    inline u16 read_ds( ) { return static_cast< u16 >( asm_read_ds( ) ); }
    inline u16 read_es( ) { return static_cast< u16 >( asm_read_es( ) ); }
    inline u16 read_fs( ) { return static_cast< u16 >( asm_read_fs( ) ); }
    inline u16 read_gs( ) { return static_cast< u16 >( asm_read_gs( ) ); }

    // SGDT/SIDT writes 10 bytes: 2-byte limit then 8-byte base no padding.
#pragma pack( push, 1 )
    struct desc_ptr_t {
        u16 limit;
        u64 base;
    };
#pragma pack( pop )
    static_assert( sizeof( desc_ptr_t ) == 10, "desc_ptr_t must be exactly 10 bytes" );

    inline desc_ptr_t sgdt( ) { desc_ptr_t d{ }; asm_sgdt( &d ); return d; }
    inline desc_ptr_t sidt( ) { desc_ptr_t d{ }; asm_sidt( &d ); return d; }

    inline u16 str( )  { return static_cast< u16 >( asm_str( ) ); }
    inline u16 sldt( ) { return static_cast< u16 >( asm_sldt( ) ); }

    inline u32 ar_bytes( u16 sel )      { return static_cast< u32 >( asm_lar( sel ) ); }
    inline u32 segment_limit( u16 sel ) { return static_cast< u32 >( asm_lsl( sel ) ); }

    inline u8   vmx_on( u64* pa )           { return static_cast< u8 >( __vmx_on( pa ) ); }
    inline void vmx_off( )                  { __vmx_off( ); }
    inline u8   vmx_vmclear( u64* pa )      { return static_cast< u8 >( __vmx_vmclear( pa ) ); }
    inline u8   vmx_vmptrld( u64* pa )      { return static_cast< u8 >( __vmx_vmptrld( pa ) ); }
    inline u8   vmx_vmwrite( u64 f, u64 v ) { return static_cast< u8 >( __vmx_vmwrite( f, v ) ); }
    inline u64  vmx_vmread( u64 f )         { u64 v = 0; __vmx_vmread( f, &v ); return v; }
    inline u8   vmx_vmlaunch( )             { return static_cast< u8 >( __vmx_vmlaunch( ) ); }
    inline u8   vmx_vmresume( )             { return static_cast< u8 >( __vmx_vmresume( ) ); }

    inline u64 vmcall( u64 code, u64 a = 0, u64 b = 0 ) { return asm_vmcall( code, a, b ); }

    inline void invept_single_context( u64 eptp ) {
        struct { u64 eptp; u64 reserved; } desc = { eptp, 0 };
        asm_invept( 1, &desc );
    }

    inline void invept_all_contexts( ) {
        struct { u64 eptp; u64 reserved; } desc = { 0, 0 };
        asm_invept( 2, &desc );
    }
}