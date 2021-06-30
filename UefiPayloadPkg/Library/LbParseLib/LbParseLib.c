/** @file
  This library will parse the linuxboot table in memory and extract those required
  information.

  Copyright (c) 2021, the u-root Authors. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/


#include <IndustryStandard/Acpi.h>
#include <IndustryStandard/SmBios.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BlParseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Linuxboot.h>
#include <Uefi/UefiBaseType.h>

#include <stdlib.h>
#include <stdint.h>
//#include <string.h>
//#include <ctype.h>

#define strncmp(a,b,n) AsciiStrnCmp((a),(b),(n))

static uint64_t parse_int(const char * s, char ** end)
{
  UINT64 x;

  if (s[0] == '0' && s[1] == 'x')
    AsciiStrHexToUint64S(s, end, &x);
  else
    AsciiStrDecimalToUint64S(s, end, &x);

  return x;
}

static int isspace(const char c)
{
  return c == ' ' || c == '\t' || c == '\n';
}


// Retrieve UefiPayloadConfig from Linuxboot's uefiboot
const UefiPayloadConfig* GetUefiPayLoadConfig() {
  const UefiPayloadConfig* config =
      (UefiPayloadConfig*)(UINTN)(PcdGet32(PcdPayloadFdMemBase) - SIZE_64KB);

  if (config->Version == UEFI_PAYLOAD_CONFIG_VERSION1
  ||  config->Version == UEFI_PAYLOAD_CONFIG_VERSION2)
    return config;

  DEBUG((DEBUG_ERROR, "Expect payload config version %016lx or %016lx, but get %016lx\n",
         UEFI_PAYLOAD_CONFIG_VERSION1, UEFI_PAYLOAD_CONFIG_VERSION2, config->Version));
  CpuDeadLoop ();
  while(1)
    ;
}

// Align the address and add memory rang to MemInfoCallback
void AddMemoryRange(IN BL_MEM_INFO_CALLBACK MemInfoCallback, IN UINTN start,
                    IN UINTN end, IN int type) {
  MEMROY_MAP_ENTRY MemoryMap;
  UINTN AlignedStart;
  UINTN AlignedEnd;
  AlignedStart = ALIGN_VALUE(start, SIZE_4KB);
  AlignedEnd = ALIGN_VALUE(end, SIZE_4KB);
  // Conservative adjustment on Memory map. This should happen when booting from
  // non UEFI bios and it may report a memory region less than 4KB.
  if (AlignedStart > start && type != LINUXBOOT_MEM_RAM) {
    AlignedStart -= SIZE_4KB;
  }
  if (AlignedEnd > end + 1 && type == LINUXBOOT_MEM_RAM) {
    AlignedEnd -= SIZE_4KB;
  }
  MemoryMap.Base = AlignedStart;
  MemoryMap.Size = AlignedEnd - AlignedStart;
  MemoryMap.Type = type;
  MemoryMap.Flag = 0;
  MemInfoCallback(&MemoryMap, NULL);
}

const char * cmdline_next(const char *cmdline, const char **option)
{
	// at the end of the string, we're done
	if (!cmdline || *cmdline == '\0')
		return NULL;

	// skip any leading whitespace
	while(isspace(*cmdline))
		cmdline++;

	// if we've hit the end of the string, we're done
	if (*cmdline == '\0')
		return NULL;

	*option = cmdline;

	// find the end of this option or the string
	while(!isspace(*cmdline) && *cmdline != '\0')
		cmdline++;

	// cmdline points to the whitespace or end of string
	return cmdline;
}

int cmdline_ints(const char *option, uint64_t args[], int max)
{
  // skip any leading text up to an '='
  const char * s = option;
  while (1)
  {
    const char c = *s++;
    if (c == '=')
      break;

    if (c == '\0' || isspace(c))
    {
      s = option;
      break;
    }
  }

  for(int i = 0 ; i < max ; i++)
  {
    char * end;
    args[i] = parse_int(s, &end);

    // end of string or end of the option?
    if (*end == '\0' || isspace(*end))
      return i+1;

    // not separator? signal an error if we have consumed any ints,
    // otherwise return 0 saying that none were found
    if (*end != ',')
      return i == 0 ? 0 : -1;

    // skip the , and get the next value
    s = end + 1;
  }

  // too many values!
  return -1;
}



/**
  Acquire the memory information from the linuxboot table in memory.

  @param  MemInfoCallback     The callback routine
  @param  Params              Pointer to the callback routine parameter

  @retval RETURN_SUCCESS     Successfully find out the memory information.
  @retval RETURN_NOT_FOUND   Failed to find the memory information.

**/
RETURN_STATUS
EFIAPI
ParseMemoryInfo(IN BL_MEM_INFO_CALLBACK MemInfoCallback, IN VOID* Params) {
  const UefiPayloadConfig* config = GetUefiPayLoadConfig();
  if (!config) {
    DEBUG((DEBUG_ERROR, "ParseMemoryInfo: Could not find UEFI Payload config\n"));
    return RETURN_SUCCESS;
  }

  if (config->Version == UEFI_PAYLOAD_CONFIG_VERSION1)
  {
    const UefiPayloadConfigV1 * config1 = &config->config.v1;
    DEBUG((DEBUG_INFO, "MemoryMap #entries: %d\n", config1->NumMemoryMapEntries));

    for (int i = 0; i < config1->NumMemoryMapEntries; i++) {
      const MemoryMapEntry* entry = &config1->MemoryMapEntries[i];
      DEBUG((DEBUG_INFO, "Start: 0x%lx End: 0x%lx Type:%d\n", entry->Start,
             entry->End, entry->Type));
      AddMemoryRange(MemInfoCallback, entry->Start, entry->End, entry->Type);
    }
  } else

  if (config->Version == UEFI_PAYLOAD_CONFIG_VERSION2)
  {
    const char * cmdline = config->config.v2.cmdline;
    const char * option;
    uint64_t args[3];

    // look for the mem=start,end,type 
    while((cmdline = cmdline_next(cmdline, &option)))
    {
      if (strncmp(option, "mem=", 4) != 0)
        continue;

      if (cmdline_ints(option, args, 3) != 3)
      {
        DEBUG((DEBUG_ERROR, "Parse error: '%a'\n", option));
        continue;
      }

      const uint64_t start = args[0];
      const uint64_t end = args[1];
      const uint64_t type = args[2];

      DEBUG((DEBUG_INFO, "Start: 0x%lx End: 0x%lx Type:%d\n",
             start, end, type));
      AddMemoryRange(MemInfoCallback, start, end, type);
    }
  }

  return RETURN_SUCCESS;
}

/**
  Acquire acpi table and smbios table from linuxboot

  @param  SystemTableInfo          Pointer to the system table info

  @retval RETURN_SUCCESS            Successfully find out the tables.
  @retval RETURN_NOT_FOUND          Failed to find the tables.

**/
RETURN_STATUS
EFIAPI
ParseSystemTable(OUT SYSTEM_TABLE_INFO* SystemTableInfo) {
  const UefiPayloadConfig* config = GetUefiPayLoadConfig();
  if (!config) {
    DEBUG((DEBUG_ERROR, "ParseSystemTable: Could not find UEFI Payload config\n"));
    return RETURN_SUCCESS;
  }

  if (config->Version == UEFI_PAYLOAD_CONFIG_VERSION1)
  {
    const UefiPayloadConfigV1 * config1 = &config->config.v1;
    SystemTableInfo->AcpiTableBase = config1->AcpiBase;
    SystemTableInfo->AcpiTableSize = config1->AcpiSize;

    SystemTableInfo->SmbiosTableBase = config1->SmbiosBase;
    SystemTableInfo->SmbiosTableSize = config1->SmbiosSize;
  } else

  if (config->Version == UEFI_PAYLOAD_CONFIG_VERSION2)
  {
    const char * cmdline = config->config.v2.cmdline;
    const char * option;
    uint64_t args[2];

    // look for the acpi config
    while((cmdline = cmdline_next(cmdline, &option)))
    {
      if (strncmp(option, "ACPI20=", 7) == 0)
      {
        const int count = cmdline_ints(option, args, 2);
        if (count < 0)
        {
          DEBUG((DEBUG_ERROR, "Parse error: '%a'\n", option));
          continue;
        }

        if (count > 0)
          SystemTableInfo->AcpiTableBase = args[0];
        if (count > 1)
          SystemTableInfo->AcpiTableSize = args[1];
      }

      if (strncmp(option, "SMBIOS=", 7) == 0)
      {
        const int count = cmdline_ints(option, args, 2);
        if (count < 0)
        {
          DEBUG((DEBUG_ERROR, "Parse error: '%a'\n", option));
          continue;
        }

        if (count > 0)
          SystemTableInfo->SmbiosTableBase = args[0];
        if (count > 1)
          SystemTableInfo->SmbiosTableSize = args[1];
      }
    }
  }

  return RETURN_SUCCESS;
}

/**
  Find the serial port information

  @param  SERIAL_PORT_INFO   Pointer to serial port info structure

  @retval RETURN_SUCCESS     Successfully find the serial port information.
  @retval RETURN_NOT_FOUND   Failed to find the serial port information .

**/
RETURN_STATUS
EFIAPI
ParseSerialInfo(OUT SERIAL_PORT_INFO* SerialPortInfo) {

  // fill in some reasonable defaults
  SerialPortInfo->BaseAddr = 0x3f8;
  SerialPortInfo->RegWidth = 1;
  SerialPortInfo->Type = 1; // uefi.SerialPortTypeIO
  SerialPortInfo->Baud = 115200;
  SerialPortInfo->InputHertz = 1843200;
  SerialPortInfo->UartPciAddr = 0;

  const UefiPayloadConfig* config = GetUefiPayLoadConfig();
  if (!config) {
    DEBUG((DEBUG_ERROR, "ParseSerialInfo: using default config\n"));
    return RETURN_SUCCESS;
  }

  if (config->Version == UEFI_PAYLOAD_CONFIG_VERSION1)
  {
    const UefiPayloadConfigV1 * config1 = &config->config.v1;
    SerialPortInfo->BaseAddr = config1->SerialConfig.BaseAddr;
    SerialPortInfo->RegWidth = config1->SerialConfig.RegWidth;
    SerialPortInfo->Type = config1->SerialConfig.Type;
    SerialPortInfo->Baud = config1->SerialConfig.Baud;
    SerialPortInfo->InputHertz = config1->SerialConfig.InputHertz;
    SerialPortInfo->UartPciAddr = config1->SerialConfig.UartPciAddr;
  } else

  if (config->Version == UEFI_PAYLOAD_CONFIG_VERSION2)
  {
    const char * cmdline = config->config.v2.cmdline;
    const char * option;
    uint64_t args[6] = {};

    while( (cmdline = cmdline_next(cmdline, &option)) )
    {
      if (strncmp(option, "serial=", 7) != 0)
        continue;
    
      const int count = cmdline_ints(option, args, 6);
      if (count < 0)
      {
         DEBUG((DEBUG_ERROR, "Parse error: %a\n", option));
         continue;
      }

      if (count > 0)
        SerialPortInfo->Baud = args[0];
      if (count > 1)
        SerialPortInfo->BaseAddr = args[1];
      if (count > 2)
        SerialPortInfo->RegWidth = args[2];
      if (count > 3)
        SerialPortInfo->Type = args[3];
      if (count > 4)
        SerialPortInfo->InputHertz = args[4];
      if (count > 5)
        SerialPortInfo->UartPciAddr = args[5];
    }
  }

  return RETURN_SUCCESS;
}

/**
  Find the video frame buffer information

  @param  GfxInfo             Pointer to the EFI_PEI_GRAPHICS_INFO_HOB structure

  @retval RETURN_SUCCESS     Successfully find the video frame buffer
information.
  @retval RETURN_NOT_FOUND   Failed to find the video frame buffer information .

**/
RETURN_STATUS
EFIAPI
ParseGfxInfo(OUT EFI_PEI_GRAPHICS_INFO_HOB* GfxInfo) {
  // Not supported
  return RETURN_NOT_FOUND;
}

/**
  Find the video frame buffer device information

  @param  GfxDeviceInfo      Pointer to the EFI_PEI_GRAPHICS_DEVICE_INFO_HOB
structure

  @retval RETURN_SUCCESS     Successfully find the video frame buffer
information.
  @retval RETURN_NOT_FOUND   Failed to find the video frame buffer information.

**/
RETURN_STATUS
EFIAPI
ParseGfxDeviceInfo(OUT EFI_PEI_GRAPHICS_DEVICE_INFO_HOB* GfxDeviceInfo) {
  return RETURN_NOT_FOUND;
}
