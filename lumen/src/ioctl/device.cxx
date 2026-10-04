#include <ntddk.h>
#include <ntstrsafe.h>
#include "device.hxx"
#include "../vmx/vmx.hxx"
#include "../vmx/ept.hxx"
#include "../arch/intrin.hxx"
#include "../../../shared/protocol.hxx"

namespace lumen::device {
    namespace {
        ULONG_PTR invept_on_this_cpu( ULONG_PTR ) {
            arch::vmcall( static_cast< u64 >( protocol::e_hypercall::invept_broadcast ) );
            return 0;
        }
    }
}

namespace lumen::device {

    namespace {
        PDEVICE_OBJECT g_dev = nullptr;

        NTSTATUS complete( PIRP irp, NTSTATUS st, ULONG_PTR info = 0 ) {
            irp->IoStatus.Status      = st;
            irp->IoStatus.Information = info;
            IoCompleteRequest( irp, IO_NO_INCREMENT );
            return st;
        }
    }

    NTSTATUS on_create( PDEVICE_OBJECT, PIRP irp ) {
        return complete( irp, STATUS_SUCCESS );
    }

    NTSTATUS on_close( PDEVICE_OBJECT, PIRP irp ) {
        return complete( irp, STATUS_SUCCESS );
    }

    NTSTATUS on_ioctl( PDEVICE_OBJECT, PIRP irp ) {
        auto* sp  = IoGetCurrentIrpStackLocation( irp );
        auto code = sp->Parameters.DeviceIoControl.IoControlCode;
        auto obuf = irp->AssociatedIrp.SystemBuffer;
        auto olen = sp->Parameters.DeviceIoControl.OutputBufferLength;

        switch ( code ) {
        case protocol::ioctl_status: {
            if ( olen < sizeof( protocol::status_t ) )
                return complete( irp, STATUS_BUFFER_TOO_SMALL );

            auto* s = static_cast< protocol::status_t* >( obuf );
            RtlZeroMemory( s, sizeof( *s ) );
            s->version          = protocol::k_version;
            s->vcpu_count       = vmx::global( ).vcpu_count( );
            s->vcpu_virtualized = vmx::global( ).launched_count( );
            s->ept_levels       = 4;
            s->ept_pml4_pa      = vmx::global( ).ept_pml4_pa( );
            RtlStringCbCopyA( s->build_tag, sizeof( s->build_tag ), "lumen 1.0" );
            return complete( irp, STATUS_SUCCESS, sizeof( *s ) );
        }

        case protocol::ioctl_virt:
            if ( vmx::global( ).virtualize_all( ) )
                return complete( irp, STATUS_SUCCESS );
            return complete( irp, STATUS_UNSUCCESSFUL );

        case protocol::ioctl_devirt:
            vmx::global( ).devirtualize_all( );
            return complete( irp, STATUS_SUCCESS );

        case protocol::ioctl_ept_hook: {
            const auto ilen = sp->Parameters.DeviceIoControl.InputBufferLength;
            if ( ilen < sizeof( protocol::ept_hook_request_t ) )
                return complete( irp, STATUS_BUFFER_TOO_SMALL );

            auto* req = static_cast< protocol::ept_hook_request_t* >( obuf );
            if ( req->byte_count == 0 || req->byte_count > protocol::k_max_hook_bytes )
                return complete( irp, STATUS_INVALID_PARAMETER );
            if ( req->offset_in_page + req->byte_count > 0x1000 )
                return complete( irp, STATUS_INVALID_PARAMETER );

            // translate kernel VA -> guest PA
            PHYSICAL_ADDRESS pa = MmGetPhysicalAddress(
                reinterpret_cast< void* >( req->target_va ) );
            if ( pa.QuadPart == 0 )
                return complete( irp, STATUS_INVALID_ADDRESS );

            // snapshot the original 4K page from the caller-provided VA
            u8 src_4k[ 0x1000 ];
            void* page_va = reinterpret_cast< void* >( req->target_va & ~0xFFFull );
            RtlCopyMemory( src_4k, page_va, sizeof( src_4k ) );

            // resolve target CR3: 0 = global, ~0 = current process
            u64 target_cr3 = req->target_cr3;
            if ( target_cr3 == protocol::k_hook_cr3_current ) {
                target_cr3 = __readcr3( );
            }

            const bool ok = vmx::global( ).ept( ).install_hook(
                static_cast< u64 >( pa.QuadPart ),
                target_cr3,
                src_4k,
                req->offset_in_page,
                req->bytes,
                req->byte_count );
            if ( !ok ) return complete( irp, STATUS_UNSUCCESSFUL );

            // broadcast INVEPT to all cpus so their TLBs pick up the new EPT state
            KeIpiGenericCall( invept_on_this_cpu, 0 );
            return complete( irp, STATUS_SUCCESS );
        }

        case protocol::ioctl_ept_unhook: {
            const auto ilen = sp->Parameters.DeviceIoControl.InputBufferLength;
            if ( ilen < sizeof( protocol::ept_unhook_request_t ) )
                return complete( irp, STATUS_BUFFER_TOO_SMALL );

            auto* req = static_cast< protocol::ept_unhook_request_t* >( obuf );
            PHYSICAL_ADDRESS pa = MmGetPhysicalAddress(
                reinterpret_cast< void* >( req->target_va ) );
            if ( pa.QuadPart == 0 )
                return complete( irp, STATUS_INVALID_ADDRESS );

            const bool ok = vmx::global( ).ept( ).remove_hook(
                static_cast< u64 >( pa.QuadPart ) );
            if ( !ok ) return complete( irp, STATUS_NOT_FOUND );

            KeIpiGenericCall( invept_on_this_cpu, 0 );
            return complete( irp, STATUS_SUCCESS );
        }

        case protocol::ioctl_ept_list: {
            if ( olen < sizeof( protocol::ept_list_response_t ) )
                return complete( irp, STATUS_BUFFER_TOO_SMALL );

            auto* resp = static_cast< protocol::ept_list_response_t* >( obuf );
            RtlZeroMemory( resp, sizeof( *resp ) );

            ept::hook_t snaps[ protocol::k_max_hooks ];
            const u32 n = vmx::global( ).ept( ).list_hooks( snaps, protocol::k_max_hooks );
            resp->count = n;
            for ( u32 i = 0; i < n; ++i ) {
                resp->entries[ i ].page_pa      = snaps[ i ].page_pa;
                resp->entries[ i ].target_cr3   = snaps[ i ].target_cr3;
                resp->entries[ i ].is_exec_mode = snaps[ i ].is_exec ? 1u : 0u;
            }
            return complete( irp, STATUS_SUCCESS, sizeof( *resp ) );
        }
        }

        return complete( irp, STATUS_INVALID_DEVICE_REQUEST );
    }

    NTSTATUS create( PDRIVER_OBJECT drv ) {
        UNICODE_STRING dev_name, sym_name;
        RtlInitUnicodeString( &dev_name, protocol::k_device_name );
        RtlInitUnicodeString( &sym_name, protocol::k_symlink_name );

        NTSTATUS st = IoCreateDevice( drv, 0, &dev_name, FILE_DEVICE_UNKNOWN,
                                      FILE_DEVICE_SECURE_OPEN, FALSE, &g_dev );
        if ( !NT_SUCCESS( st ) ) return st;

        st = IoCreateSymbolicLink( &sym_name, &dev_name );
        if ( !NT_SUCCESS( st ) ) {
            IoDeleteDevice( g_dev );
            g_dev = nullptr;
            return st;
        }

        g_dev->Flags |= DO_BUFFERED_IO;
        g_dev->Flags &= ~DO_DEVICE_INITIALIZING;
        return STATUS_SUCCESS;
    }

    void destroy( PDRIVER_OBJECT ) {
        UNICODE_STRING sym_name;
        RtlInitUnicodeString( &sym_name, protocol::k_symlink_name );
        IoDeleteSymbolicLink( &sym_name );
        if ( g_dev ) { IoDeleteDevice( g_dev ); g_dev = nullptr; }
    }
}