/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Kernel Streaming
 * FILE:            drivers/wdm/audio/backpln/portcls/api.cpp
 * PURPOSE:         Port Class driver / DriverEntry and IRP handlers
 * PROGRAMMER:      Andrew Greenwood
 *                  Johannes Anderwald
 * HISTORY:
 *                  27 Jan 07   Created
 */

#include "private.hpp"

//
//  This is called from DriverEntry so that PortCls can take care of some
//  IRPs and map some others to the main KS driver. In most cases this will
//  be the first function called by an audio driver.
//
//  First 2 parameters are from DriverEntry.
//
//  The AddDevice parameter is a driver-supplied pointer to a function which
//  typically then calls PcAddAdapterDevice (see below.)
//
NTSTATUS
NTAPI
PcInitializeAdapterDriver(
    IN  PDRIVER_OBJECT DriverObject,
    IN  PUNICODE_STRING RegistryPathName,
    IN  PDRIVER_ADD_DEVICE AddDevice)
{
    DPRINT("PcInitializeAdapterDriver\n");
    PC_ASSERT_IRQL_EQUAL(PASSIVE_LEVEL);

    // Our IRP handlers
    DPRINT("Setting IRP handlers\n");
    DriverObject->MajorFunction[IRP_MJ_CREATE] = PcDispatchIrp;
    DriverObject->MajorFunction[IRP_MJ_PNP] = PcDispatchIrp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = PcDispatchIrp;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = PcDispatchIrp;
    DriverObject->MajorFunction[IRP_MJ_SHUTDOWN] = PcDispatchIrp;

    // The driver-supplied AddDevice
    DriverObject->DriverExtension->AddDevice = AddDevice;

    // KS handles these
    DPRINT("Setting KS function handlers\n");
    KsSetMajorFunctionHandler(DriverObject, IRP_MJ_CLOSE);
    KsSetMajorFunctionHandler(DriverObject, IRP_MJ_DEVICE_CONTROL);
    KsSetMajorFunctionHandler(DriverObject, IRP_MJ_FLUSH_BUFFERS);
    KsSetMajorFunctionHandler(DriverObject, IRP_MJ_QUERY_SECURITY);
    KsSetMajorFunctionHandler(DriverObject, IRP_MJ_READ);
    KsSetMajorFunctionHandler(DriverObject, IRP_MJ_SET_SECURITY);
    KsSetMajorFunctionHandler(DriverObject, IRP_MJ_WRITE);

    DPRINT("PortCls has finished initializing the adapter driver\n");

    return STATUS_SUCCESS;
}

//
//  Typically called by a driver's AddDevice function, which is set when
//  calling PcInitializeAdapterDriver. This performs some common driver
//  operations, such as creating a device extension.
//
//  The StartDevice parameter is a driver-supplied function which gets
//  called in response to IRP_MJ_PNP / IRP_MN_START_DEVICE.
//
NTSTATUS
NTAPI
PcAddAdapterDevice(
    IN  PDRIVER_OBJECT DriverObject,
    IN  PDEVICE_OBJECT PhysicalDeviceObject,
    IN  PCPFNSTARTDEVICE StartDevice,
    IN  ULONG MaxObjects,
    IN  ULONG DeviceExtensionSize)
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    PDEVICE_OBJECT fdo;
    PDEVICE_OBJECT PrevDeviceObject;
    PPCLASS_DEVICE_EXTENSION portcls_ext = NULL;

    DPRINT("PcAddAdapterDevice called\n");
    PC_ASSERT_IRQL_EQUAL(PASSIVE_LEVEL);

    if (!DriverObject || !PhysicalDeviceObject || !StartDevice)
    {
        return STATUS_INVALID_PARAMETER;
    }

    // check if the DeviceExtensionSize is provided
    if ( DeviceExtensionSize < PORT_CLASS_DEVICE_EXTENSION_SIZE )
    {
        // driver does not need a device extension 
        if ( DeviceExtensionSize != 0 )
        {
            // DeviceExtensionSize must be zero
            return STATUS_INVALID_PARAMETER;
        }
        // set size to our extension size
        DeviceExtensionSize = PORT_CLASS_DEVICE_EXTENSION_SIZE;
    }

    // create the device
    status = IoCreateDevice(DriverObject,
                            DeviceExtensionSize,
                            NULL,
                            FILE_DEVICE_KS,
                            FILE_AUTOGENERATED_DEVICE_NAME | FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &fdo);

    if (!NT_SUCCESS(status))
    {
        DPRINT("IoCreateDevice() failed with status 0x%08lx\n", status);
        return status;
    }

    // Obtain the new device extension
    portcls_ext = (PPCLASS_DEVICE_EXTENSION) fdo->DeviceExtension;
    // initialize the device extension
    RtlZeroMemory(portcls_ext, DeviceExtensionSize);
    // allocate create item
    portcls_ext->CreateItems = (PKSOBJECT_CREATE_ITEM)AllocateItem(NonPagedPool, MaxObjects * sizeof(KSOBJECT_CREATE_ITEM), TAG_PORTCLASS);

    if (!portcls_ext->CreateItems)
    {
        // not enough resources
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto cleanup;
    }

    // store max subdevice count
    portcls_ext->MaxSubDevices = MaxObjects;
    // store the physical device object
    portcls_ext->PhysicalDeviceObject = PhysicalDeviceObject;
    // set up the start device function
    portcls_ext->StartDevice = StartDevice;
    // initialize timer lock
    KeInitializeSpinLock(&portcls_ext->TimerListLock);
    // initialize timer list
    InitializeListHead(&portcls_ext->TimerList);
    // initialize io timer
    IoInitializeTimer(fdo, PcIoTimerRoutine, NULL);
    // start the io timer
    IoStartTimer(fdo);

    // set io flags
    fdo->Flags |= DO_DIRECT_IO | DO_POWER_PAGABLE;
    // clear initializing flag
    fdo->Flags &= ~ DO_DEVICE_INITIALIZING;

    // allocate the device header
    status = KsAllocateDeviceHeader(&portcls_ext->KsDeviceHeader, MaxObjects, portcls_ext->CreateItems);
    // did we succeed
    if (!NT_SUCCESS(status))
    {
        goto cleanup;
    }

    // attach device to device stack
    PrevDeviceObject = IoAttachDeviceToDeviceStack(fdo, PhysicalDeviceObject);
    // did we succeed
    if (PrevDeviceObject)
    {
        // store the device object in the device header
        //KsSetDevicePnpBaseObject(portcls_ext->KsDeviceHeader, fdo, PrevDeviceObject);
        portcls_ext->PrevDeviceObject = PrevDeviceObject;
    }
    else
    {
        // return error code
        status = STATUS_UNSUCCESSFUL;
        goto cleanup;
    }

    // register shutdown notification
    IoRegisterShutdownNotification(PhysicalDeviceObject);

    return status;

cleanup:

    if (portcls_ext->KsDeviceHeader)
    {
        // free the device header
        KsFreeDeviceHeader(portcls_ext->KsDeviceHeader);
    }

    if (portcls_ext->CreateItems)
    {
        // free previously allocated create items
        FreeItem(portcls_ext->CreateItems, TAG_PORTCLASS);
    }

    // delete created fdo
    IoDeleteDevice(fdo);


    return status;
}

NTSTATUS
NTAPI
PcRegisterSubdevice(
    IN  PDEVICE_OBJECT DeviceObject,
    IN  PWCHAR Name,
    IN  PUNKNOWN Unknown)
{
    PPCLASS_DEVICE_EXTENSION DeviceExt;
    NTSTATUS Status;
    ISubdevice *SubDevice;
    UNICODE_STRING SymbolicLinkName;
    PSUBDEVICE_DESCRIPTOR SubDeviceDescriptor;
    ULONG Index;
    UNICODE_STRING RefName;
    PSYMBOLICLINK_ENTRY SymEntry;

    DPRINT("PcRegisterSubdevice DeviceObject %p Name %S Unknown %p\n", DeviceObject, Name, Unknown);

    PC_ASSERT_IRQL_EQUAL(PASSIVE_LEVEL);

    // check if all parameters are valid
    if (!DeviceObject || !Name || !Unknown)
    {
        DPRINT("PcRegisterSubdevice invalid parameter\n");
        return STATUS_INVALID_PARAMETER;
    }

    // get device extension
    DeviceExt = (PPCLASS_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

    if (!DeviceExt)
    {
        // should not happen
        DbgBreakPoint();
        return STATUS_UNSUCCESSFUL;
    }

    // look up our undocumented interface
    Status = Unknown->QueryInterface(IID_ISubdevice, (LPVOID*)&SubDevice);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("No ISubdevice interface\n");
        // the provided port driver doesnt support ISubdevice
        return STATUS_INVALID_PARAMETER;
    }

    // get the subdevice descriptor
    Status = SubDevice->GetDescriptor(&SubDeviceDescriptor);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("Failed to get subdevice descriptor %x\n", Status);
        SubDevice->Release();
        return STATUS_UNSUCCESSFUL;
    }

    // add an create item to the device header
    Status = KsAddObjectCreateItemToDeviceHeader(DeviceExt->KsDeviceHeader, PcCreateItemDispatch, (PVOID)SubDevice, Name, NULL);
    if (!NT_SUCCESS(Status))
    {
        // failed to attach
        SubDevice->Release();
        DPRINT("KsAddObjectCreateItemToDeviceHeader failed with %x\n", Status);
        return Status;
    }

    // initialize reference string
    RtlInitUnicodeString(&RefName, Name);
    RtlInitUnicodeString(&SubDeviceDescriptor->RefString, Name);

    for(Index = 0; Index < SubDeviceDescriptor->InterfaceCount; Index++)
    {
        // FIXME
        // check if reference string with that name already exists
        
        Status = IoRegisterDeviceInterface(DeviceExt->PhysicalDeviceObject,
                                           &SubDeviceDescriptor->Interfaces[Index],
                                           &RefName,
                                           &SymbolicLinkName);

        if (NT_SUCCESS(Status))
        {
            // activate device interface
            IoSetDeviceInterfaceState(&SymbolicLinkName, TRUE);
            // allocate symbolic link entry
            SymEntry = (PSYMBOLICLINK_ENTRY)AllocateItem(NonPagedPool, sizeof(SYMBOLICLINK_ENTRY), TAG_PORTCLASS);
            if (SymEntry)
            {
                // initialize symbolic link item
                RtlInitUnicodeString(&SymEntry->SymbolicLink, SymbolicLinkName.Buffer);
                // store item
                InsertTailList(&SubDeviceDescriptor->SymbolicLinkList, &SymEntry->Entry);
            }
            else
            {
                // allocating failed
                RtlFreeUnicodeString(&SymbolicLinkName);
            }
        }
    }

    // release SubDevice reference
    SubDevice->Release();

    return STATUS_SUCCESS;
}
