/******************************************************************************
 *
 * ListNetworks - CLI tool to list wireless networks on AmigaOS 3
 *
 * Based on ShowSanaDev by Philippe CARPENTIER
 * Compilable with SAS/C 6.59
 *
 ******************************************************************************/

#include <devices/sana2.h>
#include <devices/sana2specialstats.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <exec/exec.h>
#include <exec/types.h>
#include <exec/errors.h>
#include <exec/memory.h>
#include <exec/devices.h>
#include <utility/tagitem.h>
#include <utility/utility.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>

/******************************************************************************
 *
 * NewStyle Device (NSD) definitions
 * (from devices/newstyle.h - not present in all SDK installations)
 *
 ******************************************************************************/

#ifndef NSCMD_DEVICEQUERY
#define NSCMD_DEVICEQUERY  0x4000

#define NSDEVTYPE_UNKNOWN     0
#define NSDEVTYPE_GAMEPORT    1
#define NSDEVTYPE_TIMER       2
#define NSDEVTYPE_KEYBOARD    3
#define NSDEVTYPE_INPUT       4
#define NSDEVTYPE_TRACKDISK   5
#define NSDEVTYPE_CONSOLE     6
#define NSDEVTYPE_SANA2       7
#define NSDEVTYPE_AUDIOARD    8
#define NSDEVTYPE_CLIPBOARD   9
#define NSDEVTYPE_PRINTER    10
#define NSDEVTYPE_SERIAL     11
#define NSDEVTYPE_PARALLEL   12

struct NSDeviceQueryResult
{
	ULONG   nsdqr_DevQueryFormat;
	ULONG   nsdqr_SizeAvailable;
	UWORD   nsdqr_DeviceType;
	UWORD   nsdqr_DeviceSubType;
	UWORD * nsdqr_SupportedCommands;
};

#endif /* NSCMD_DEVICEQUERY */

/******************************************************************************
 *
 * SANA2 Wireless definitions
 * (from devices/sana2wireless.h - not present in all SDK installations)
 *
 ******************************************************************************/

#ifndef S2_GETSIGNALQUALITY

/* Wireless commands */
#define S2_GETSIGNALQUALITY 0xC010
#define S2_GETNETWORKS      0xC011
#define S2_SETOPTIONS       0xC012
#define S2_SETKEY           0xC013
#define S2_GETNETWORKINFO   0xC014
#define S2_READMGMT         0xC015
#define S2_WRITEMGMT        0xC016
#define S2_GETCRYPTTYPES    0xC017

/* Tags for getting/setting wireless network info */
#define S2INFO_SSID             (TAG_USER + 0)
#define S2INFO_BSSID            (TAG_USER + 1)
#define S2INFO_AuthTypes        (TAG_USER + 2)
#define S2INFO_AssocID          (TAG_USER + 3)
#define S2INFO_Encryption       (TAG_USER + 4)
#define S2INFO_PortType         (TAG_USER + 5)
#define S2INFO_BeaconInterval   (TAG_USER + 6)
#define S2INFO_Channel          (TAG_USER + 7)
#define S2INFO_Signal           (TAG_USER + 8)
#define S2INFO_Noise            (TAG_USER + 9)
#define S2INFO_Capabilities     (TAG_USER + 10)
#define S2INFO_InfoElements     (TAG_USER + 11)
#define S2INFO_WPAInfo          (TAG_USER + 12)
#define S2INFO_Band             (TAG_USER + 13)
#define S2INFO_DefaultKeyNo     (TAG_USER + 14)

/* Encryption types */
#define S2ENC_NONE  0
#define S2ENC_WEP   1
#define S2ENC_TKIP  2
#define S2ENC_CCMP  3

/* Signal quality structure */
struct Sana2SignalQuality
{
	LONG SignalLevel;
	LONG NoiseLevel;
};

#endif /* S2_GETSIGNALQUALITY */

/******************************************************************************
 *
 * Version string
 *
 ******************************************************************************/

static const char * verstag = "$VER: ListNetworks 1.0 (06.02.2026) Renaud Schweingruber";

/******************************************************************************
 *
 * Defines
 *
 ******************************************************************************/

#define POOLPUDDLESIZE 32768
#define POOLTHRESHSIZE 32768
#define MAXDEVICES     16

#define ITERATE_LIST(list, type, node) \
	for (node = (type)((struct List *)(list))->lh_Head; \
	     ((struct Node *)node)->ln_Succ; \
	     node = (type)((struct Node *)node)->ln_Succ)

/******************************************************************************
 *
 * ReadArgs template
 *
 ******************************************************************************/

#define TEMPLATE "DEVICE/K,UNIT/K/N,VERBOSE/S,SHORT/S"

enum {
	ARG_DEVICE = 0,
	ARG_UNIT,
	ARG_VERBOSE,
	ARG_SHORT,
	ARG_COUNT
};

/******************************************************************************
 *
 * Externs
 *
 ******************************************************************************/

extern struct ExecBase * SysBase;
extern struct DosLibrary * DOSBase;
extern struct Library * UtilityBase;

/******************************************************************************
 *
 * StrLen()
 *
 ******************************************************************************/

static ULONG StrLen(STRPTR s)
{
	STRPTR p = s;
	while (*p++);
	return (ULONG)(p - s);
}

/******************************************************************************
 *
 * Strncpy()
 *
 ******************************************************************************/

static VOID Strncpy(STRPTR dst, STRPTR src, ULONG len)
{
	while (len-- && *src)
		*dst++ = *src++;
	*dst = 0;
}

/******************************************************************************
 *
 * GetEncryptionName()
 *
 ******************************************************************************/

static STRPTR GetEncryptionName(UBYTE type)
{
	switch (type)
	{
	case S2ENC_NONE: return "NONE";
	case S2ENC_CCMP: return "CCMP";
	case S2ENC_TKIP: return "TKIP";
	case S2ENC_WEP:  return "WEP";
	default:         return "?";
	}
}

/******************************************************************************
 *
 * GetHardwareTypeName()
 *
 ******************************************************************************/

static STRPTR GetHardwareTypeName(ULONG hwType)
{
	switch (hwType)
	{
	case S2WireType_Ethernet:  return "Ethernet";
	case S2WireType_IEEE802:   return "IEEE802";
	case S2WireType_Arcnet:    return "Arcnet";
	case S2WireType_LocalTalk: return "LocalTalk";
	case S2WireType_DyLAN:     return "DyLAN";
	case S2WireType_AmokNet:   return "AmokNet";
	case S2WireType_Liana:     return "Liana";
	case S2WireType_PPP:       return "PPP";
	case S2WireType_SLIP:      return "SLIP";
	case S2WireType_CSLIP:     return "CSLIP";
	case S2WireType_PLIP:      return "PLIP";
	default:                   return "Unknown";
	}
}

/******************************************************************************
 *
 * PrintError() - print SANA2 error info
 *
 ******************************************************************************/

static VOID PrintError(struct IOSana2Req * req)
{
	PutStr("  Error: ");

	switch (req->ios2_Req.io_Error) {
	case IOERR_OPENFAIL:      PutStr("IOERR_OPENFAIL");      break;
	case IOERR_ABORTED:       PutStr("IOERR_ABORTED");       break;
	case IOERR_NOCMD:         PutStr("IOERR_NOCMD");         break;
	case IOERR_BADLENGTH:     PutStr("IOERR_BADLENGTH");     break;
	case IOERR_BADADDRESS:    PutStr("IOERR_BADADDRESS");    break;
	case IOERR_UNITBUSY:      PutStr("IOERR_UNITBUSY");      break;
	case IOERR_SELFTEST:      PutStr("IOERR_SELFTEST");      break;
	case S2ERR_NO_ERROR:      PutStr("S2ERR_NO_ERROR");      break;
	case S2ERR_NO_RESOURCES:  PutStr("S2ERR_NO_RESOURCES");  break;
	case S2ERR_BAD_ARGUMENT:  PutStr("S2ERR_BAD_ARGUMENT");  break;
	case S2ERR_BAD_STATE:     PutStr("S2ERR_BAD_STATE");     break;
	case S2ERR_BAD_ADDRESS:   PutStr("S2ERR_BAD_ADDRESS");   break;
	case S2ERR_MTU_EXCEEDED:  PutStr("S2ERR_MTU_EXCEEDED");  break;
	case S2ERR_NOT_SUPPORTED: PutStr("S2ERR_NOT_SUPPORTED"); break;
	case S2ERR_SOFTWARE:      PutStr("S2ERR_SOFTWARE");      break;
	case S2ERR_OUTOFSERVICE:  PutStr("S2ERR_OUTOFSERVICE");  break;
	default:
		Printf("%ld", req->ios2_Req.io_Error);
		break;
	}

	PutStr("\n");
}

/******************************************************************************
 *
 * IsCommandSupported()
 *
 ******************************************************************************/

static BOOL IsCommandSupported(struct NSDeviceQueryResult * nsdqr, UWORD command)
{
	UWORD * commands;

	if (nsdqr == NULL || nsdqr->nsdqr_SupportedCommands == NULL)
		return FALSE;

	commands = nsdqr->nsdqr_SupportedCommands;

	while (*commands)
	{
		if (command == *commands)
			return TRUE;

		commands++;
	}

	return FALSE;
}

/******************************************************************************
 *
 * FindSana2Devices() - enumerate all SANA2 devices in the system
 *
 ******************************************************************************/

static ULONG FindSana2Devices(STRPTR * nameArray, ULONG maxDevices)
{
	struct Device * device;
	ULONG count = 0;

	Forbid();

	ITERATE_LIST(&SysBase->DeviceList, struct Device *, device)
	{
		struct MsgPort * msgPort;

		if ((msgPort = CreateMsgPort()) != NULL)
		{
			struct IOStdReq * io;

			if ((io = (struct IOStdReq *)CreateIORequest(msgPort, 10 * sizeof(struct IOStdReq))) != NULL)
			{
				STRPTR deviceName = device->dd_Library.lib_Node.ln_Name;

				Permit();

				if (OpenDevice(deviceName, 0, (struct IORequest *)io, 0) == 0)
				{
					struct NSDeviceQueryResult __aligned nsdqr;

					nsdqr.nsdqr_DevQueryFormat    = 0;
					nsdqr.nsdqr_SizeAvailable     = 0;
					nsdqr.nsdqr_DeviceType        = 0;
					nsdqr.nsdqr_DeviceSubType     = 0;
					nsdqr.nsdqr_SupportedCommands = NULL;

					io->io_Command = NSCMD_DEVICEQUERY;
					io->io_Data    = &nsdqr;
					io->io_Length  = sizeof(struct NSDeviceQueryResult);

					if (DoIO((struct IORequest *)io) == 0)
					{
						if (nsdqr.nsdqr_DeviceType == NSDEVTYPE_SANA2)
						{
							ULONG nameLen = StrLen(deviceName);

							if (nameLen > 0)
							{
								if (nameArray[count] = AllocVec(nameLen, MEMF_PUBLIC | MEMF_CLEAR))
								{
									Strncpy(nameArray[count], deviceName, nameLen);
									count++;

									if (count >= maxDevices)
									{
										CloseDevice((struct IORequest *)io);
										DeleteIORequest((struct IORequest *)io);
										DeleteMsgPort(msgPort);
										return count;
									}
								}
							}
						}
					}

					CloseDevice((struct IORequest *)io);
				}

				Forbid();

				DeleteIORequest((struct IORequest *)io);
			}

			DeleteMsgPort(msgPort);
		}
	}

	Permit();

	return count;
}

/******************************************************************************
 *
 * FreeSana2DeviceNames()
 *
 ******************************************************************************/

static VOID FreeSana2DeviceNames(STRPTR * nameArray, ULONG count)
{
	ULONG i;

	for (i = 0; i < count; i++)
	{
		if (nameArray[i])
		{
			FreeVec(nameArray[i]);
			nameArray[i] = NULL;
		}
	}
}

/******************************************************************************
 *
 * PrintSeparator()
 *
 ******************************************************************************/

static VOID PrintSeparator(VOID)
{
	PutStr("---------+-------------------+------+----------+--------\n");
}

/******************************************************************************
 *
 * PrintNetworkHeader()
 *
 ******************************************************************************/

static VOID PrintNetworkHeader(VOID)
{
	PutStr("\n");
	PrintSeparator();
	PutStr(" Signal  | BSSID             | Chan | Band     | SSID\n");
	PrintSeparator();
}

/******************************************************************************
 *
 * main()
 *
 ******************************************************************************/

ULONG main(ULONG argc, STRPTR * argv)
{
	struct RDArgs * rdargs;
	LONG args[ARG_COUNT];
	ULONG result = RETURN_FAIL;

	STRPTR deviceName = NULL;
	ULONG  unitNumber = 0;
	BOOL   verbose    = FALSE;
	BOOL   shortMode  = FALSE;

	STRPTR deviceNames[MAXDEVICES];
	ULONG  deviceCount = 0;
	ULONG  i;

	struct MsgPort * msgPort = NULL;
	struct IOStdReq * ioReq = NULL;
	struct NSDeviceQueryResult __aligned nsdqr;
	struct Sana2DeviceQuery __aligned devQuery;
	struct Sana2SignalQuality __aligned sigQuality;
	APTR poolHeader = NULL;

	/* Initialize args array */
	for (i = 0; i < ARG_COUNT; i++)
		args[i] = 0;

	/* Parse command line arguments */

	if ((rdargs = ReadArgs(TEMPLATE, args, NULL)) != NULL)
	{
		if (args[ARG_DEVICE])
			deviceName = (STRPTR)args[ARG_DEVICE];

		if (args[ARG_UNIT])
			unitNumber = *((LONG *)args[ARG_UNIT]);

		verbose = (BOOL)args[ARG_VERBOSE];
		shortMode = (BOOL)args[ARG_SHORT];
	}
	else
	{
		PrintFault(IoErr(), "ListNetworks");
		return RETURN_ERROR;
	}

	if (!shortMode)
		PutStr("ListNetworks 1.0 - Wireless network scanner for AmigaOS\n");

	/* If no device specified, find all SANA2 devices and list them */

	if (deviceName == NULL)
	{
		if (!shortMode)
			PutStr("\nScanning for SANA2 network devices...\n\n");

		deviceCount = FindSana2Devices(deviceNames, MAXDEVICES);

		if (deviceCount == 0)
		{
			PutStr("No SANA2 network devices found.\n");
			FreeArgs(rdargs);
			return RETURN_WARN;
		}

		if (!shortMode)
		{
			Printf("Found %ld SANA2 device(s):\n\n", deviceCount);

			for (i = 0; i < deviceCount; i++)
			{
				Printf("  %ld: %s\n", i + 1, deviceNames[i]);
			}
		}

		/* Use the first device found by default */
		deviceName = deviceNames[0];

		if (!shortMode)
			PutStr("\nUsing first device. Use DEVICE=<n> to specify another.\n");
	}

	/* Open the SANA2 device */

	if (!shortMode)
		Printf("\nOpening device: %s unit %ld\n", deviceName, unitNumber);
	if ((msgPort = CreateMsgPort()) == NULL)
	{
		PutStr("Error: Cannot create message port.\n");
		goto cleanup;
	}

	if ((ioReq = (struct IOStdReq *)CreateIORequest(msgPort, 10 * sizeof(struct IOStdReq))) == NULL)
	{
		PutStr("Error: Cannot create IO request.\n");
		goto cleanup;
	}

	if (OpenDevice(deviceName, unitNumber, (struct IORequest *)ioReq, 0) != 0)
	{
		Printf("Error: Cannot open device '%s' unit %ld.\n", deviceName, unitNumber);
		ioReq->io_Device = NULL;
		goto cleanup;
	}

	/* NSD query to verify it's a SANA2 device */

	nsdqr.nsdqr_DevQueryFormat    = 0;
	nsdqr.nsdqr_SizeAvailable     = 0;
	nsdqr.nsdqr_DeviceType        = 0;
	nsdqr.nsdqr_DeviceSubType     = 0;
	nsdqr.nsdqr_SupportedCommands = NULL;

	ioReq->io_Command = NSCMD_DEVICEQUERY;
	ioReq->io_Data    = &nsdqr;
	ioReq->io_Length  = sizeof(struct NSDeviceQueryResult);

	if (DoIO((struct IORequest *)ioReq) != 0 || nsdqr.nsdqr_DeviceType != NSDEVTYPE_SANA2)
	{
		PutStr("Error: Device is not a SANA2 network device.\n");
		goto cleanup;
	}

	if (!shortMode)
		PutStr("Device confirmed as SANA2 network device.\n");

	/* Show device info if verbose */

	if (verbose)
	{
		struct IOSana2Req * s2req = (struct IOSana2Req *)ioReq;
		struct Device * device = ioReq->io_Device;

		Printf("\nDevice info:\n");
		Printf("  Name     : %s\n", device->dd_Library.lib_Node.ln_Name);
		Printf("  Version  : %ld.%ld\n",
			(ULONG)device->dd_Library.lib_Version,
			(ULONG)device->dd_Library.lib_Revision);
		Printf("  Open cnt : %ld\n", (ULONG)device->dd_Library.lib_OpenCnt);

		/* S2_DEVICEQUERY */
		s2req->ios2_Req.io_Command = S2_DEVICEQUERY;
		s2req->ios2_StatData = &devQuery;

		devQuery.SizeAvailable  = sizeof(struct Sana2DeviceQuery);
		devQuery.SizeSupplied   = 0;
		devQuery.DevQueryFormat = 0;
		devQuery.DeviceLevel    = 0;
		devQuery.AddrFieldSize  = 0;
		devQuery.BPS            = 0;
		devQuery.HardwareType   = 0;
		devQuery.MTU            = 0;

		if (DoIO((struct IORequest *)s2req) == S2ERR_NO_ERROR)
		{
			Printf("  Type     : %s\n", GetHardwareTypeName(devQuery.HardwareType));
			Printf("  Speed    : %ld bps\n", devQuery.BPS);
			Printf("  MTU      : %ld bytes\n", (ULONG)devQuery.MTU);
			Printf("  Addr size: %ld bits\n", (ULONG)devQuery.AddrFieldSize);
		}

		/* S2_GETSTATIONADDRESS */
		s2req->ios2_Req.io_Command = S2_GETSTATIONADDRESS;

		if (DoIO((struct IORequest *)s2req) == S2ERR_NO_ERROR)
		{
			UBYTE * addr = s2req->ios2_SrcAddr;
			Printf("  MAC addr : %02lx:%02lx:%02lx:%02lx:%02lx:%02lx\n",
				(ULONG)addr[0], (ULONG)addr[1],
				(ULONG)addr[2], (ULONG)addr[3],
				(ULONG)addr[4], (ULONG)addr[5]);
		}

		/* S2_GETSIGNALQUALITY */
		if (IsCommandSupported(&nsdqr, S2_GETSIGNALQUALITY))
		{
			s2req->ios2_Req.io_Command = S2_GETSIGNALQUALITY;
			s2req->ios2_StatData = &sigQuality;

			if (DoIO((struct IORequest *)s2req) == S2ERR_NO_ERROR)
			{
				Printf("  Signal   : %ld dBm\n", sigQuality.SignalLevel);
				Printf("  Noise    : %ld dBm\n", sigQuality.NoiseLevel);
				Printf("  SNR      : %ld dB\n",
					sigQuality.SignalLevel - sigQuality.NoiseLevel);
			}
		}

		/* S2_GETNETWORKINFO - show current connected network */
		if (IsCommandSupported(&nsdqr, S2_GETNETWORKINFO))
		{
			poolHeader = CreatePool(MEMF_PUBLIC | MEMF_CLEAR, POOLPUDDLESIZE, POOLTHRESHSIZE);

			if (poolHeader)
			{
				s2req->ios2_Req.io_Command = S2_GETNETWORKINFO;
				s2req->ios2_Data = poolHeader;

				if (DoIO((struct IORequest *)s2req) == S2ERR_NO_ERROR)
				{
					struct TagItem * tags;

					if (tags = (struct TagItem *)s2req->ios2_StatData)
					{
						UBYTE * bssid = (UBYTE *)GetTagData(S2INFO_BSSID, 0, tags);

						PutStr("\nConnected network:\n");
						Printf("  SSID     : %s\n",
							GetTagData(S2INFO_SSID, 0, tags));

						if (bssid)
						{
							Printf("  BSSID    : %02lx:%02lx:%02lx:%02lx:%02lx:%02lx\n",
								(ULONG)bssid[0], (ULONG)bssid[1],
								(ULONG)bssid[2], (ULONG)bssid[3],
								(ULONG)bssid[4], (ULONG)bssid[5]);
						}

						Printf("  Channel  : %ld\n",
							GetTagData(S2INFO_Channel, 0, tags));
						Printf("  Band     : %sGHz\n",
							GetTagData(S2INFO_Band, 0, tags) ? "2.4" : "5");
					}
				}

				DeletePool(poolHeader);
				poolHeader = NULL;
			}
		}

		/* S2_GETCRYPTTYPES */
		if (IsCommandSupported(&nsdqr, S2_GETCRYPTTYPES))
		{
			poolHeader = CreatePool(MEMF_PUBLIC | MEMF_CLEAR, POOLPUDDLESIZE, POOLTHRESHSIZE);

			if (poolHeader)
			{
				s2req->ios2_Req.io_Command = S2_GETCRYPTTYPES;
				s2req->ios2_Data = poolHeader;

				if (DoIO((struct IORequest *)s2req) == S2ERR_NO_ERROR)
				{
					UBYTE * types = (UBYTE *)s2req->ios2_StatData;
					ULONG len = s2req->ios2_DataLength;

					PutStr("  Crypto   :");

					for (i = 0; i < len; i++)
					{
						Printf(" %s", GetEncryptionName(types[i]));
					}

					PutStr("\n");
				}

				DeletePool(poolHeader);
				poolHeader = NULL;
			}
		}
	}

	/* Scan for available wireless networks using S2_GETNETWORKS */

	if (!IsCommandSupported(&nsdqr, S2_GETNETWORKS))
	{
		PutStr("\nThis device does not support wireless network scanning.\n");
		PutStr("(S2_GETNETWORKS command not available)\n");
		result = RETURN_WARN;
		goto cleanup;
	}

	poolHeader = CreatePool(MEMF_PUBLIC | MEMF_CLEAR, POOLPUDDLESIZE, POOLTHRESHSIZE);

	if (poolHeader == NULL)
	{
		PutStr("Error: Cannot allocate memory pool.\n");
		goto cleanup;
	}

	{
		struct IOSana2Req * s2req = (struct IOSana2Req *)ioReq;

		if (!shortMode)
			PutStr("\nScanning for wireless networks...\n");

		s2req->ios2_Req.io_Command = S2_GETNETWORKS;
		s2req->ios2_Data = poolHeader;

		if (DoIO((struct IORequest *)s2req) == S2ERR_NO_ERROR)
		{
			ULONG numNetworks = s2req->ios2_DataLength;
			APTR * buffer = (APTR *)s2req->ios2_StatData;

			if (numNetworks == 0)
			{
				PutStr("\nNo wireless networks found.\n");
			}
			else
			{
				if (shortMode)
				{
					for (i = 0; i < numNetworks; i++)
					{
						struct TagItem * tags = (struct TagItem *)buffer[i];

						STRPTR ssid = (STRPTR)GetTagData(S2INFO_SSID, (ULONG)"<hidden>", tags);
						ULONG  band = GetTagData(S2INFO_Band, 0, tags);

						Printf("%s (%s GHz)\n", ssid, band ? "2.4" : "5");
					}
				}
				else
				{
					Printf("\n%ld wireless network(s) found:\n", numNetworks);

					PrintNetworkHeader();

					for (i = 0; i < numNetworks; i++)
					{
						struct TagItem * tags = (struct TagItem *)buffer[i];

						UBYTE * bssid   = (UBYTE *)GetTagData(S2INFO_BSSID, 0, tags);
						STRPTR  ssid    = (STRPTR)GetTagData(S2INFO_SSID, (ULONG)"<hidden>", tags);
						ULONG   channel = GetTagData(S2INFO_Channel, 0, tags);
						LONG    signal  = GetTagData(S2INFO_Signal, -90, tags);
						LONG    noise   = GetTagData(S2INFO_Noise, -90, tags);
						ULONG   band    = GetTagData(S2INFO_Band, 0, tags);
						LONG    snr     = signal - noise;

						if (bssid)
						{
							Printf(" %4ld dB | %02lx:%02lx:%02lx:%02lx:%02lx:%02lx | %4ld | %sGHz | %s\n",
								snr,
								(ULONG)bssid[0], (ULONG)bssid[1],
								(ULONG)bssid[2], (ULONG)bssid[3],
								(ULONG)bssid[4], (ULONG)bssid[5],
								channel,
								band ? "2.4  " : "5    ",
								ssid);
						}
						else
						{
							Printf(" %4ld dB | --:--:--:--:--:-- | %4ld | %sGHz | %s\n",
								snr,
								channel,
								band ? "2.4  " : "5    ",
								ssid);
						}
					}

					PrintSeparator();
				}
			}

			result = RETURN_OK;
		}
		else
		{
			PutStr("\nError: Failed to scan for networks.\n");
			PrintError(s2req);
			result = RETURN_ERROR;
		}
	}

cleanup:

	if (poolHeader)
		DeletePool(poolHeader);

	if (ioReq)
	{
		if (ioReq->io_Device)
			CloseDevice((struct IORequest *)ioReq);

		DeleteIORequest((struct IORequest *)ioReq);
	}

	if (msgPort)
		DeleteMsgPort(msgPort);

	if (deviceCount > 0)
		FreeSana2DeviceNames(deviceNames, deviceCount);

	if (rdargs)
		FreeArgs(rdargs);

	return result;
}

/******************************************************************************
 *
 * End of file
 *
 ******************************************************************************/
