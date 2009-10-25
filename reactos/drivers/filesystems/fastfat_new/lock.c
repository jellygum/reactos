/*
 * PROJECT:         ReactOS FAT file system driver
 * LICENSE:         GNU GPLv3 as published by the Free Software Foundation
 * FILE:            drivers/filesystems/fastfat/lock.c
 * PURPOSE:         Lock support routines
 * PROGRAMMERS:     Aleksey Bragin (aleksey@reactos.org)
 */

/* INCLUDES *****************************************************************/

#define NDEBUG
#include "fastfat.h"

/* FUNCTIONS ****************************************************************/

NTSTATUS
NTAPI
FatLockControl(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    DPRINT1("FatLockControl()\n");
    return STATUS_NOT_IMPLEMENTED;
}

VOID
NTAPI
FatOplockComplete(IN PVOID Context,
                  IN PIRP Irp)
{
    UNIMPLEMENTED;
}

VOID
NTAPI
FatPrePostIrp(IN PVOID Context,
              IN PIRP Irp)
{
    UNIMPLEMENTED;
}

/* EOF */
