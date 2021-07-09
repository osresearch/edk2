/** @file
  This driver will report some MMIO/IO resources to dxe core, extract smbios and acpi
  tables from bootloader.

  Copyright (c) 2014 - 2020, Intel Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/
#include "BlSupportDxe.h"

#include <Protocol/DevicePath.h>
#include <Library/DevicePathLib.h>
#include <Protocol/RamDisk.h>
#include <Library/MemoryAllocationLib.h>

/**
  Reserve MMIO/IO resource in GCD

  @param  IsMMIO        Flag of whether it is mmio resource or io resource.
  @param  GcdType       Type of the space.
  @param  BaseAddress   Base address of the space.
  @param  Length        Length of the space.
  @param  Alignment     Align with 2^Alignment
  @param  ImageHandle   Handle for the image of this driver.

  @retval EFI_SUCCESS   Reserve successful
**/
EFI_STATUS
ReserveResourceInGcd (
  IN BOOLEAN               IsMMIO,
  IN UINTN                 GcdType,
  IN EFI_PHYSICAL_ADDRESS  BaseAddress,
  IN UINT64                Length,
  IN UINTN                 Alignment,
  IN EFI_HANDLE            ImageHandle
  )
{
  EFI_STATUS               Status;

  if (IsMMIO) {
    Status = gDS->AddMemorySpace (
                    GcdType,
                    BaseAddress,
                    Length,
                    EFI_MEMORY_UC
                    );
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "Failed to add memory space :0x%lx 0x%lx\n",
        BaseAddress,
        Length
        ));
    }
    ASSERT_EFI_ERROR (Status);
    Status = gDS->AllocateMemorySpace (
                    EfiGcdAllocateAddress,
                    GcdType,
                    Alignment,
                    Length,
                    &BaseAddress,
                    ImageHandle,
                    NULL
                    );
    ASSERT_EFI_ERROR (Status);
  } else {
    Status = gDS->AddIoSpace (
                    GcdType,
                    BaseAddress,
                    Length
                    );
    ASSERT_EFI_ERROR (Status);
    Status = gDS->AllocateIoSpace (
                    EfiGcdAllocateAddress,
                    GcdType,
                    Alignment,
                    Length,
                    &BaseAddress,
                    ImageHandle,
                    NULL
                    );
    ASSERT_EFI_ERROR (Status);
  }
  return Status;
}


static void EFIAPI ramdisk_callback(EFI_EVENT event, void * context)
{
  const SYSTEM_TABLE_INFO *SystemTableInfo = context;
  const unsigned char * ramdisk_base = (const void*) SystemTableInfo->RamDiskBase;
  const UINTN ramdisk_size = SystemTableInfo->RamDiskSize;

  if (!ramdisk_base || !ramdisk_size)
    return;

  EFI_STATUS                 Status;
  EFI_RAM_DISK_PROTOCOL      *RamDisk;
  EFI_DEVICE_PATH_PROTOCOL   *DevicePath;
  EFI_GUID                   *RamDiskType = &gEfiVirtualDiskGuid;

  Status = gBS->LocateProtocol(&gEfiRamDiskProtocolGuid, NULL, (VOID**) &RamDisk);
  // if there is no protocol, we've been signalled too early. we'll try again later
  if (EFI_ERROR (Status))
    return;

  // it is necessary to copy the ramdisk from the kexec allocated memory to uefi allocated
  // memory. otherwise the memory will be reclaimed during the boot process, leading to
  // a corrupt BCD hive or other propblems.
  const unsigned char * ramdisk_copy = AllocateCopyPool(ramdisk_size, (const void*) ramdisk_base);
  if (!ramdisk_copy)
  {
    DEBUG((EFI_D_ERROR, "allocate %d bytes for ramdisk copy failed\n", ramdisk_size));
    return;
  }

/*
  for(int i = 0x200 ; i < 0x240 ; i++)
    DEBUG ((EFI_D_INFO, "%02x", ramdisk_base[i]));
  DEBUG ((EFI_D_INFO, "\n"));
*/

  Status = RamDisk->Register(
       (UINTN) ramdisk_copy,
       ramdisk_size,
       RamDiskType,
       NULL,
       &DevicePath
  );

  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "ramdisk_setup: Failed to register RAM Disk - %r\n", Status));
    return;
  }

  VOID * Temp = ConvertDevicePathToText(DevicePath, TRUE, TRUE);
  DEBUG ((EFI_D_INFO, "ramdisk_setup: ram disk %p + %x: device path %S\n", ramdisk_copy, ramdisk_size, Temp));
  FreePool(Temp);
}


/**
  Main entry for the bootloader support DXE module.

  @param[in] ImageHandle    The firmware allocated handle for the EFI image.
  @param[in] SystemTable    A pointer to the EFI System Table.

  @retval EFI_SUCCESS       The entry point is executed successfully.
  @retval other             Some error occurs when executing this entry point.

**/
EFI_STATUS
EFIAPI
BlDxeEntryPoint (
  IN EFI_HANDLE              ImageHandle,
  IN EFI_SYSTEM_TABLE        *SystemTable
  )
{
  EFI_STATUS Status;
  EFI_HOB_GUID_TYPE          *GuidHob;
  SYSTEM_TABLE_INFO          *SystemTableInfo;
  EFI_PEI_GRAPHICS_INFO_HOB  *GfxInfo;
  ACPI_BOARD_INFO            *AcpiBoardInfo;

  Status = EFI_SUCCESS;
  //
  // Report MMIO/IO Resources
  //
  Status = ReserveResourceInGcd (TRUE, EfiGcdMemoryTypeMemoryMappedIo, 0xFEC00000, SIZE_4KB, 0, ImageHandle); // IOAPIC
  ASSERT_EFI_ERROR (Status);

  Status = ReserveResourceInGcd (TRUE, EfiGcdMemoryTypeMemoryMappedIo, 0xFED00000, SIZE_1KB, 0, ImageHandle); // HPET
  ASSERT_EFI_ERROR (Status);

  //
  // Find the system table information guid hob
  //
  GuidHob = GetFirstGuidHob (&gUefiSystemTableInfoGuid);
  ASSERT (GuidHob != NULL);
  SystemTableInfo = (SYSTEM_TABLE_INFO *)GET_GUID_HOB_DATA (GuidHob);

  //
  // Install Acpi Table
  //
  if (SystemTableInfo->AcpiTableBase != 0 && SystemTableInfo->AcpiTableSize != 0) {
    DEBUG ((DEBUG_ERROR, "Install Acpi Table at 0x%lx, length 0x%x\n", SystemTableInfo->AcpiTableBase, SystemTableInfo->AcpiTableSize));
    Status = gBS->InstallConfigurationTable (&gEfiAcpiTableGuid, (VOID *)(UINTN)SystemTableInfo->AcpiTableBase);
    ASSERT_EFI_ERROR (Status);
  }

  //
  // Install Smbios Table
  //
  if (SystemTableInfo->SmbiosTableBase != 0) {
    if (SystemTableInfo->SmbiosTableSize == sizeof(SMBIOS_TABLE_ENTRY_POINT)) {
      DEBUG((DEBUG_ERROR, "Install Smbios Table at 0x%lx, length 0x%x\n",
             SystemTableInfo->SmbiosTableBase,
             SystemTableInfo->SmbiosTableSize));
      Status = gBS->InstallConfigurationTable(
          &gEfiSmbiosTableGuid,
          (VOID *)(UINTN)SystemTableInfo->SmbiosTableBase);
      ASSERT_EFI_ERROR(Status);
    } else if (SystemTableInfo->SmbiosTableSize == sizeof(SMBIOS_TABLE_3_0_ENTRY_POINT)) {
      DEBUG((DEBUG_ERROR, "Install Smbios Table at 0x%lx, length 0x%x\n",
             SystemTableInfo->SmbiosTableBase,
             SystemTableInfo->SmbiosTableSize));
      Status = gBS->InstallConfigurationTable(
          &gEfiSmbios3TableGuid,
          (VOID *)(UINTN)SystemTableInfo->SmbiosTableBase);
      ASSERT_EFI_ERROR(Status);
    }
  }

  //
  // Find the frame buffer information and update PCDs
  //
  GuidHob = GetFirstGuidHob (&gEfiGraphicsInfoHobGuid);
  if (GuidHob != NULL) {
    GfxInfo = (EFI_PEI_GRAPHICS_INFO_HOB *)GET_GUID_HOB_DATA (GuidHob);
    Status = PcdSet32S (PcdVideoHorizontalResolution, GfxInfo->GraphicsMode.HorizontalResolution);
    ASSERT_EFI_ERROR (Status);
    Status = PcdSet32S (PcdVideoVerticalResolution, GfxInfo->GraphicsMode.VerticalResolution);
    ASSERT_EFI_ERROR (Status);
    Status = PcdSet32S (PcdSetupVideoHorizontalResolution, GfxInfo->GraphicsMode.HorizontalResolution);
    ASSERT_EFI_ERROR (Status);
    Status = PcdSet32S (PcdSetupVideoVerticalResolution, GfxInfo->GraphicsMode.VerticalResolution);
    ASSERT_EFI_ERROR (Status);
  }

  //
  // Set PcdPciExpressBaseAddress and PcdPciExpressBaseSize by HOB info
  //
  GuidHob = GetFirstGuidHob (&gUefiAcpiBoardInfoGuid);
  if (GuidHob != NULL) {
    AcpiBoardInfo = (ACPI_BOARD_INFO *)GET_GUID_HOB_DATA (GuidHob);
    Status = PcdSet64S (PcdPciExpressBaseAddress, AcpiBoardInfo->PcieBaseAddress);
    ASSERT_EFI_ERROR (Status);
    Status = PcdSet64S (PcdPciExpressBaseSize, AcpiBoardInfo->PcieBaseSize);
    ASSERT_EFI_ERROR (Status);
  }

  // Wait for the RamDiskProtocol to become available
  static EFI_EVENT ramdisk_event;
  static void * ramdisk_registration;

  Status = gBS->CreateEvent(EVT_NOTIFY_SIGNAL, TPL_CALLBACK, ramdisk_callback, SystemTableInfo, &ramdisk_event);
  ASSERT_EFI_ERROR(Status);
  Status = gBS->RegisterProtocolNotify(&gEfiRamDiskProtocolGuid, ramdisk_event, &ramdisk_registration);
  ASSERT_EFI_ERROR(Status);
  Status = gBS->SignalEvent(ramdisk_event);
  ASSERT_EFI_ERROR(Status);

  return EFI_SUCCESS;
}
