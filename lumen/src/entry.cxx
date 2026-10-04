#include <ntddk.h>
#include "arch/intrin.hxx"
#include "arch/vmx_defs.hxx"
#include "vmx/vmx.hxx"
#include "ioctl/device.hxx"

using namespace lumen;

extern "C" NTSTATUS driver_create( PDEVICE_OBJECT d, PIRP irp ) { return device::on_create( d, irp ); }
extern "C" NTSTATUS driver_close( PDEVICE_OBJECT d, PIRP irp )  { return device::on_close( d, irp ); }
extern "C" NTSTATUS driver_ioctl( PDEVICE_OBJECT d, PIRP irp )  { return device::on_ioctl( d, irp ); }

extern "C" void driver_unload( PDRIVER_OBJECT drv ) {
    vmx::global( ).devirtualize_all( );
    vmx::global( ).destroy( );
    device::destroy( drv );
}

extern "C" NTSTATUS DriverEntry( PDRIVER_OBJECT drv, PUNICODE_STRING ) {
    const auto c1 = arch::cpuid( 1 );
    if ( !( c1.ecx & ( 1u << 5 ) ) ) return STATUS_NOT_SUPPORTED;

    // Microsoft Hv owns VMX and would crash us;
    // VMware/KVM/VirtualBox with nested VT-x enabled report their vendor
    // but still give us a real nested VMX, so we continue.
    if ( c1.ecx & ( 1u << 31 ) ) {
        const auto hv = arch::cpuid( 0x40000000 );
        if ( hv.ebx == 'rciM' ) return STATUS_HV_FEATURE_UNAVAILABLE;
    }

    NTSTATUS st = device::create( drv );
    if ( !NT_SUCCESS( st ) ) return st;

    drv->DriverUnload                           = driver_unload;
    drv->MajorFunction[ IRP_MJ_CREATE ]         = driver_create;
    drv->MajorFunction[ IRP_MJ_CLOSE ]          = driver_close;
    drv->MajorFunction[ IRP_MJ_DEVICE_CONTROL ] = driver_ioctl;

    if ( !vmx::global( ).init( ) ) {
        device::destroy( drv );
        return STATUS_UNSUCCESSFUL;
    }

    if ( !vmx::global( ).virtualize_all( ) ) {
        vmx::global( ).devirtualize_all( );
        vmx::global( ).destroy( );
        device::destroy( drv );
        return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}