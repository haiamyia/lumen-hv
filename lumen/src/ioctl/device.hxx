#pragma once

#include <ntddk.h>

namespace lumen::device {

    NTSTATUS create( PDRIVER_OBJECT drv );
    void     destroy( PDRIVER_OBJECT drv );

    NTSTATUS on_create( PDEVICE_OBJECT, PIRP irp );
    NTSTATUS on_close( PDEVICE_OBJECT, PIRP irp );
    NTSTATUS on_ioctl( PDEVICE_OBJECT, PIRP irp );
}