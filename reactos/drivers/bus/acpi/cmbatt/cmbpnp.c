/*
 * PROJECT:         ReactOS ACPI-Compliant Control Method Battery
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            boot/drivers/bus/acpi/cmbatt/cmbpnp.c
 * PURPOSE:         Plug-and-Play IOCTL/IRP Handling
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include "cmbatt.h"

/* FUNCTIONS ******************************************************************/

NTSTATUS
NTAPI
CmBattIoCompletion(PDEVICE_OBJECT DeviceObject,
                   PIRP Irp,
                   PKEVENT Event)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
CmBattGetAcpiInterfaces(PDEVICE_OBJECT DeviceObject,
                        PACPI_INTERFACE_STANDARD AcpiInterface)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

VOID
NTAPI
CmBattDestroyFdo(IN PDEVICE_OBJECT DeviceObject)
{
    PAGED_CODE();
    if (CmBattDebug & 0x220) DbgPrint("CmBattDestroyFdo, Battery.\n");

    /* Delete the device */
    IoDeleteDevice(DeviceObject);
    if (CmBattDebug & 0x220) DbgPrint("CmBattDestroyFdo: done.\n");
}

NTSTATUS
NTAPI
CmBattRemoveDevice(IN PDEVICE_OBJECT DeviceObject,
                   IN PIRP Irp)
{
    PCMBATT_DEVICE_EXTENSION DeviceExtension;
    PVOID Context;
    DeviceExtension = DeviceObject->DeviceExtension;
    if (CmBattDebug & 2)
        DbgPrint("CmBattRemoveDevice: CmBatt (%x), Type %d, _UID %d\n",
                 DeviceExtension,
                 DeviceExtension->FdoType,
                 DeviceExtension->DeviceId);
                 
    /* Make sure it's safe to go ahead */
    IoReleaseRemoveLockAndWait(&DeviceExtension->RemoveLock, 0);

    /* Check for pending power IRP */
    if (DeviceExtension->PowerIrp)
    {
        /* Cancel and clear */
        IoCancelIrp(DeviceExtension->PowerIrp);
        DeviceExtension->PowerIrp = NULL;
    }
    
    /* Check what type of FDO is being removed */
    Context = DeviceExtension->AcpiInterface.Context;
    if (DeviceExtension->FdoType == CmBattBattery)
    {
        /* Unregister battery FDO */
        DeviceExtension->AcpiInterface.UnregisterForDeviceNotifications(Context,
                                                                        (PVOID)CmBattNotifyHandler);
        CmBattWmiDeRegistration(DeviceExtension);
        if (!NT_SUCCESS(BatteryClassUnload(DeviceExtension->ClassData))) ASSERT(FALSE);
    }
    else
    {
        /* Unregister AC adapter FDO */
        DeviceExtension->AcpiInterface.UnregisterForDeviceNotifications(Context,
                                                                        (PVOID)CmBattNotifyHandler);
        CmBattWmiDeRegistration(DeviceExtension);
        AcAdapterPdo = NULL;
    }
    
    /* Detach and delete */
    IoDetachDevice(DeviceExtension->AttachedDevice);
    IoDeleteDevice(DeviceExtension->DeviceObject);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CmBattPowerDispatch(PDEVICE_OBJECT DeviceObject,
                    PIRP Irp)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
CmBattPnpDispatch(PDEVICE_OBJECT DeviceObject,
                  PIRP Irp)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
CmBattCreateFdo(IN PDRIVER_OBJECT DriverObject,
                IN PDEVICE_OBJECT DeviceObject,
                IN ULONG DeviceExtensionSize,
                IN PDEVICE_OBJECT *NewDeviceObject)
{
    PDEVICE_OBJECT FdoDeviceObject;
    HANDLE KeyHandle;
    PCMBATT_DEVICE_EXTENSION FdoExtension;
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION PartialInfo = (PVOID)Buffer;
    NTSTATUS Status;
    UNICODE_STRING KeyString;
    ULONG UniqueId;
    ULONG ResultLength;
    PAGED_CODE();
    if (CmBattDebug & 0x220) DbgPrint("CmBattCreateFdo: Entered\n");
    
    /* Get unique ID */
    Status = CmBattGetUniqueId(DeviceObject, &UniqueId);
    if (!NT_SUCCESS(Status))
    {
        /* Assume 0 */
        UniqueId = 0;
        if (CmBattDebug & 2)
          DbgPrint("CmBattCreateFdo: Error %x from _UID, assuming unit #0\n", Status);
    }
    
    /* Create the FDO */
    Status = IoCreateDevice(DriverObject,
                            DeviceExtensionSize,
                            0,
                            FILE_DEVICE_BATTERY,
                            FILE_DEVICE_SECURE_OPEN,
                            0,
                            &FdoDeviceObject);
    if (!NT_SUCCESS(Status))
    {
        /* Fail */
        if (CmBattDebug & 0xC)
            DbgPrint("CmBattCreateFdo: error (0x%x) creating device object\n", Status);
        return Status;
    }
    
    /* Set FDO flags */
    FdoDeviceObject->Flags |= DO_BUFFERED_IO;
    FdoDeviceObject->Flags |= DO_MAP_IO_BUFFER;
    FdoDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    /* Initialize the extension */
    FdoExtension = FdoDeviceObject->DeviceExtension;
    RtlZeroMemory(FdoExtension, DeviceExtensionSize);
    FdoExtension->DeviceObject = FdoDeviceObject;
    FdoExtension->FdoDeviceObject = FdoDeviceObject;
    FdoExtension->PdoDeviceObject = DeviceObject;
    
    /* Attach to ACPI */
    FdoExtension->AttachedDevice = IoAttachDeviceToDeviceStack(FdoDeviceObject,
                                                               DeviceObject);
    if (!FdoExtension->AttachedDevice)
    {
        /* Destroy and fail */
        CmBattDestroyFdo(FdoExtension->FdoDeviceObject);
        if (CmBattDebug & 0xC)
            DbgPrint("CmBattCreateFdo: IoAttachDeviceToDeviceStack failed.\n");
        return STATUS_UNSUCCESSFUL;
    }
    
    /* Get ACPI interface for EVAL */
    Status = CmBattGetAcpiInterfaces(FdoExtension->AttachedDevice,
                                     &FdoExtension->AcpiInterface);
    if (!FdoExtension->AttachedDevice)
    {
        /* Detach, destroy, and fail */
        IoDetachDevice(FdoExtension->AttachedDevice);
        CmBattDestroyFdo(FdoExtension->FdoDeviceObject);
        if (CmBattDebug & 0xC)
            DbgPrint("CmBattCreateFdo: Could not get ACPI interfaces: %x\n", Status);
        return STATUS_UNSUCCESSFUL;
    }

    /* Setup the rest of the extension */
    ExInitializeFastMutex(&FdoExtension->FastMutex);
    IoInitializeRemoveLock(&FdoExtension->RemoveLock, 0, 0, 0);
    FdoExtension->HandleCount = 0;
    FdoExtension->WaitWakeEnable = FALSE;
    FdoExtension->DeviceId = UniqueId;
    FdoExtension->DeviceName = NULL;
    FdoExtension->DelayNotification = FALSE;
    FdoExtension->ArFlag = 0;
    
    /* Open the device key */
    Status = IoOpenDeviceRegistryKey(DeviceObject,
                                     PLUGPLAY_REGKEY_DEVICE,
                                     KEY_READ,
                                     &KeyHandle);
    if (NT_SUCCESS(Status))
    {
        /* Read wait wake value */
        RtlInitUnicodeString(&KeyString, L"WaitWakeEnabled");
        Status = ZwQueryValueKey(KeyHandle,
                                 &KeyString,
                                 KeyValuePartialInformation,
                                 PartialInfo,
                                 sizeof(Buffer),
                                 &ResultLength);
        if (NT_SUCCESS(Status))
        {
            /* Set value */
            FdoExtension->WaitWakeEnable = *(PULONG)PartialInfo->Data;
        }
        
        /* Close the handle */
        ZwClose(KeyHandle);
    }

    /* Return success and the new FDO */
    *NewDeviceObject = FdoDeviceObject;
    if (CmBattDebug & 0x220)
        DbgPrint("CmBattCreateFdo: Created FDO %x\n", FdoDeviceObject);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CmBattAddBattery(IN PDRIVER_OBJECT DriverObject,
                 IN PDEVICE_OBJECT DeviceObject)
{
    BATTERY_MINIPORT_INFO MiniportInfo;
    NTSTATUS Status;
    PDEVICE_OBJECT FdoDeviceObject;
    PCMBATT_DEVICE_EXTENSION FdoExtension; 
    PAGED_CODE();
    if (CmBattDebug & 0x220)
        DbgPrint("CmBattAddBattery: pdo %x\n", DeviceObject);

    Status = CmBattCreateFdo(DriverObject,
                             DeviceObject,
                             sizeof(CMBATT_DEVICE_EXTENSION),
                             &FdoDeviceObject);
    if (!NT_SUCCESS(Status))
    {
        if (CmBattDebug & 0xC)
            DbgPrint("CmBattAddBattery: error (0x%x) creating Fdo\n", Status);
        return Status;
    }

    /* Build the FDO extensio, check if we support trip points */
    FdoExtension = FdoDeviceObject->DeviceExtension;
    FdoExtension->FdoType = CmBattBattery;
    FdoExtension->Started = 0;
    FdoExtension->NotifySent = TRUE;
    InterlockedExchange(&FdoExtension->ArLockValue, 0);
    FdoExtension->TripPointValue = BATTERY_UNKNOWN_CAPACITY;
    FdoExtension->Tag = 0;
    FdoExtension->InterruptTime = KeQueryInterruptTime();
    FdoExtension->TripPointSet = CmBattSetTripPpoint(FdoExtension, 0) !=
                                 STATUS_OBJECT_NAME_NOT_FOUND;
       
    /* Setup the battery miniport information structure */
    RtlZeroMemory(&MiniportInfo, sizeof(MiniportInfo));
    MiniportInfo.Pdo = DeviceObject;
    MiniportInfo.MajorVersion = BATTERY_CLASS_MAJOR_VERSION;
    MiniportInfo.MinorVersion = BATTERY_CLASS_MINOR_VERSION;
    MiniportInfo.Context = FdoExtension;
    MiniportInfo.QueryTag = (PVOID)CmBattQueryTag;
    MiniportInfo.QueryInformation = (PVOID)CmBattQueryInformation;
    MiniportInfo.SetInformation = NULL;
    MiniportInfo.QueryStatus = (PVOID)CmBattQueryStatus;
    MiniportInfo.SetStatusNotify = (PVOID)CmBattSetStatusNotify;
    MiniportInfo.DisableStatusNotify = (PVOID)CmBattDisableStatusNotify;
    MiniportInfo.DeviceName = FdoExtension->DeviceName;
 
    /* Register with the class driver */
    Status = BatteryClassInitializeDevice(&MiniportInfo, &FdoExtension->ClassData);
    if (!NT_SUCCESS(Status))
    {
        IoDetachDevice(FdoExtension->AttachedDevice);
        CmBattDestroyFdo(FdoExtension->FdoDeviceObject);
        if (CmBattDebug & 0xC)
            DbgPrint("CmBattAddBattery: error (0x%x) registering with class\n", Status);
        return Status;
    }

    /* Register WMI */
    Status = CmBattWmiRegistration(FdoExtension);
    if (!NT_SUCCESS(Status))
    {
        if (CmBattDebug & 0xC)
            DbgPrint("CmBattAddBattery: Could not register as a WMI provider, status = %Lx\n", Status);
        return Status;
    }
    
    /* Register ACPI */
    Status = FdoExtension->AcpiInterface.RegisterForDeviceNotifications(FdoExtension->AcpiInterface.Context,
                                                                        (PVOID)CmBattNotifyHandler,
                                                                        FdoExtension);
    if (!NT_SUCCESS(Status))
    {
        CmBattWmiDeRegistration(FdoExtension);
        BatteryClassUnload(FdoExtension->ClassData);
        IoDetachDevice(FdoExtension->AttachedDevice);
        CmBattDestroyFdo(FdoExtension->FdoDeviceObject);
        if (CmBattDebug & 0xC)
            DbgPrint("CmBattAddBattery: Could not register for battery notify, status = %Lx\n", Status);
    }
    
    /* Return status */
    return Status;
}

NTSTATUS
NTAPI
CmBattAddAcAdapter(IN PDRIVER_OBJECT DriverObject,
                   IN PDEVICE_OBJECT PdoDeviceObject)
{
    PDEVICE_OBJECT FdoDeviceObject;
    NTSTATUS Status;
    PCMBATT_DEVICE_EXTENSION DeviceExtension;
    PAGED_CODE();
    if (CmBattDebug & 0x220)
        DbgPrint("CmBattAddAcAdapter: pdo %x\n", PdoDeviceObject);

    /* Check if we already have an AC adapter */
    if (AcAdapterPdo)
    {
        /* Don't do anything */
        if (CmBattDebug & 0xC)
            DbgPrint("CmBatt: Second AC adapter found.  Current version of driver only supports 1 aadapter.\n");
    }
    else
    {
        /* Set this as the AC adapter's PDO */
        AcAdapterPdo = PdoDeviceObject;
    }
    
    /* Create the FDO for the adapter */
    Status = CmBattCreateFdo(DriverObject,
                             PdoDeviceObject,
                             sizeof(CMBATT_DEVICE_EXTENSION),
                             &FdoDeviceObject);
    if (!NT_SUCCESS(Status))
    {
        /* Fail */
        if (CmBattDebug & 0xC)
            DbgPrint("CmBattAddAcAdapter: error (0x%x) creating Fdo\n", Status);
        return Status;
    }
    
    /* Set the type and do WMI registration */
    DeviceExtension = FdoDeviceObject->DeviceExtension;
    DeviceExtension->FdoType = CmBattAcAdapter;
    Status = CmBattWmiRegistration(DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        /* We can go on without WMI */
        if (CmBattDebug & 0xC)
            DbgPrint("CmBattAddBattery: Could not register as a WMI provider, status = %Lx\n", Status);
    }
    
    /* Register with ACPI */
    Status = DeviceExtension->AcpiInterface.RegisterForDeviceNotifications(DeviceExtension->AcpiInterface.Context,
                                                                           (PVOID)CmBattNotifyHandler,
                                                                           DeviceExtension);
    if (!(NT_SUCCESS(Status)) && (CmBattDebug & 0xC))
        DbgPrint("CmBattAddAcAdapter: Could not register for power notify, status = %Lx\n", Status);

    /* Send the first manual notification */
    CmBattNotifyHandler(DeviceExtension, ACPI_BATT_NOTIFY_STATUS);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CmBattAddDevice(IN PDRIVER_OBJECT DriverObject,
                IN PDEVICE_OBJECT PdoDeviceObject)
{
    NTSTATUS Status;
    HANDLE KeyHandle;
    ULONG ResultLength;
    UNICODE_STRING KeyString;
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION PartialInfo = (PVOID)Buffer;
    ULONG PowerSourceType;
    PAGED_CODE();
    if (CmBattDebug & 0x220)
        DbgPrint("CmBattAddDevice: Entered with pdo %x\n", PdoDeviceObject);
    
    /* Make sure we have a PDO */
    if (!PdoDeviceObject)
    {
        /* Should not be having as one */
        if (CmBattDebug & 0x24) DbgPrint("CmBattAddDevice: Asked to do detection\n");
        return STATUS_NO_MORE_ENTRIES;
    }
    
    /* Open the driver key */
    Status = IoOpenDeviceRegistryKey(PdoDeviceObject,
                                     PLUGPLAY_REGKEY_DRIVER,
                                     KEY_READ,
                                     &KeyHandle);
    if (!NT_SUCCESS(Status))
    {
        if (CmBattDebug & 0xC)
            DbgPrint("CmBattAddDevice: Could not get the software branch: %x\n", Status);
        return Status;
    }

    /* Read the power source type */
    RtlInitUnicodeString(&KeyString, L"PowerSourceType");
    Status = ZwQueryValueKey(KeyHandle,
                             &KeyString,
                             KeyValuePartialInformation,
                             PartialInfo,
                             sizeof(Buffer),
                             &ResultLength);
    ZwClose(KeyHandle);
    if (!NT_SUCCESS(Status))
    {
        /* We need the data, fail without it */
        if (CmBattDebug & 0xC)
            DbgPrint("CmBattAddDevice: Could not read the power type identifier: %x\n", Status);
        return Status;
    }
    
    /* Check what kind of power source this is */
    PowerSourceType = *(PULONG)PartialInfo->Data;
    if (PowerSourceType == 1)
    {
        /* Create an AC adapter */
        Status = CmBattAddAcAdapter(DriverObject, PdoDeviceObject);
    }
    else if (PowerSourceType == 0)
    {
        /* Create a battery */
        Status = CmBattAddBattery(DriverObject, PdoDeviceObject);
    }
    else
    {
        /* Unknown type, fail */
        if (CmBattDebug & 0xC)
            DbgPrint("CmBattAddDevice: Invalid POWER_SOURCE_TYPE == %d \n", PowerSourceType);
        return STATUS_UNSUCCESSFUL;
    }
    
    /* Return whatever the FDO creation routine did */
    return Status;
}

/* EOF */
