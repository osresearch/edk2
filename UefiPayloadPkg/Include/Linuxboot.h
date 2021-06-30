/** @file
  LinuxBoot PEI module include file.
**/
#ifndef _LINUXBOOT_PEI_H_INCLUDED_
#define _LINUXBOOT_PEI_H_INCLUDED_

#if defined(_MSC_VER)
#pragma warning(disable : 4200)
#endif

#pragma pack(1)
typedef struct SerialPortConfigStruct {
  UINT32 Type;
  UINT32 BaseAddr;
  UINT32 Baud;
  UINT32 RegWidth;
  UINT32 InputHertz;
  UINT32 UartPciAddr;
} SerialPortConfig;

typedef struct MemoryMapEntryStruct {
  UINT64 Start;
  UINT64 End;
  UINT32 Type;
} MemoryMapEntry;

typedef struct {
  UINT64 AcpiBase;
  UINT64 AcpiSize;
  UINT64 SmbiosBase;
  UINT64 SmbiosSize;
  SerialPortConfig SerialConfig;
  UINT32 NumMemoryMapEntries;
  MemoryMapEntry MemoryMapEntries[0];
} UefiPayloadConfigV1;

typedef struct UefiPayloadConfigStruct {
  UINT64 Version;
  union {
    UefiPayloadConfigV1 v1;
    struct {
      char cmdline[0]; // up to 64 KB
    } v2;
  } config;
} UefiPayloadConfig;
#pragma pack()

// magic version config is "LnxBoot1"
#define UEFI_PAYLOAD_CONFIG_VERSION1 1
#define UEFI_PAYLOAD_CONFIG_VERSION2 0x31746f6f42786e4cULL

#define LINUXBOOT_MEM_RAM 1
#define LINUXBOOT_MEM_DEFAULT 2
#define LINUXBOOT_MEM_ACPI 3
#define LINUXBOOT_MEM_NVS 4
#define LINUXBOOT_MEM_RESERVED 5

#endif  // _LINUXBOOT_PEI_H_INCLUDED_
