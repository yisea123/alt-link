
#include "stdafx.h"
#include "ADIv5.h"
#include "JEP106.h"

#define _DBGPRT printf

int32_t ADIv5::Component::readCid()
{
	int ret;
	uint32_t data;
	
	cid.raw = 0;

	ret = ap.read(base + 0xFF0, &data);
	if (ret != OK)
		return ret;
	cid.uint8[0] = data;

	ret = ap.read(base + 0xFF4, &data);
	if (ret != OK)
		return ret;
	cid.uint8[1] = data;

	ret = ap.read(base + 0xFF8, &data);
	if (ret != OK)
		return ret;
	cid.uint8[2] = data;

	ret = ap.read(base + 0xFFC, &data);
	if (ret != OK)
		return ret;
	cid.uint8[3] = data;

	return OK;
}

int32_t ADIv5::Component::readPid()
{
	int ret;
	uint32_t data;
	
	pid.raw = 0;

	ret = ap.read(base + 0xFE0, &data);
	if (ret != OK)
		return ret;
	pid.uint8[0] = data;

	ret = ap.read(base + 0xFE4, &data);
	if (ret != OK)
		return ret;
	pid.uint8[1] = data;

	ret = ap.read(base + 0xFE8, &data);
	if (ret != OK)
		return ret;
	pid.uint8[2] = data;

	ret = ap.read(base + 0xFEC, &data);
	if (ret != OK)
		return ret;
	pid.uint8[3] = data;

	ret = ap.read(base + 0xFD0, &data);
	if (ret != OK)
		return ret;
	pid.uint8[4] = data;

	return OK;
}

enum
{
	ARM_PART_SCS_M3		= 0,
	ARM_PART_ITM_M347	= 1,
	ARM_PART_DWT_M347	= 2,
	ARM_PART_FPB_M34	= 3,
	ARM_PART_CTI_M7		= 6,
	ARM_PART_SCS_M00P	= 8,
	ARM_PART_DWT_M00P	= 0xA,
	ARM_PART_BPU_M00P	= 0xB,
	ARM_PART_SCS_M47	= 0xC,
	ARM_PART_FPB_M7		= 0xE,
	ARM_PART_TPIU_M3	= 0x923,
	ARM_PART_ETM_M4		= 0x925,
	ARM_PART_TPIU_M4	= 0x9A1
};

const char* ADIv5::Component::getName()
{
	if (cid.ComponentClass == CID::ROM_TABLE)
		return "ROM_TABLE";
	
	if (cid.ComponentClass == CID::GENERIC_IP)
	{
		if (pid.isARM())
		{
			switch (pid.PART)
			{
			case ARM_PART_SCS_M3:
				return "Cortex-M3 SCS (System Control Space)";
			case ARM_PART_ITM_M347:
				return "Cortex-M3/M4/M7 ITM (Instrumentation Trace Macrocell unit)";
			case ARM_PART_DWT_M347:
				return "Cortex-M3/M4/M7 DWT (Data Watchpoint and Trace unit)";
			case ARM_PART_FPB_M34:
				return "Cortex-M3/M4 FPB (Flash Patch and Breakpoint unit)";
			case ARM_PART_CTI_M7:
				return "Cortex-M7 CTI (Cross Trigger Interface)";
			case ARM_PART_SCS_M00P:
				return "Cortex-M0/M0+ SCS (System Control Space)";
			case ARM_PART_DWT_M00P:
				return "Cortex-M0/M0+ DWT (Data Watchpoint and Trace unit)";
			case ARM_PART_BPU_M00P:
				return "Cortex-M0/M0+ BPU (Break Point Unit)";	// Subset of FPB
			case ARM_PART_SCS_M47:
				return "Cortex-M4/M7(w/o FPU) SCS (System Control Space)";
			case ARM_PART_FPB_M7:
				return "Cortex-M7 FPB (Flash Patch and Breakpoint unit)";
			}
		}
	}
	
	if (cid.ComponentClass == CID::DEBUG_COMPONENT)
	{
		if (pid.isARM())
		{
			switch (pid.PART)
			{
			case ARM_PART_TPIU_M3:
				return "Cortex-M3 TPIU (Trace Port Interface Unit)";
			case ARM_PART_ETM_M4:
				return "Cortex-M4 ETM (Embedded Trace Macrocell)";
			case ARM_PART_TPIU_M4:
				return "Cortex-M4 TPIU (Trace Port Interface Unit)";
			}
		}

		//if (pid.isARM() && pid.PART == 0x)
		//	return "Cortex-M3 ETM";
	}

	return "UNKNOWN";
}

bool ADIv5::Component::isARMv6MSCS()
{
	if (cid.ComponentClass == CID::GENERIC_IP)
		if (pid.isARM())
			if (pid.PART == ARM_PART_SCS_M3 ||
				pid.PART == ARM_PART_SCS_M00P ||
				pid.PART == ARM_PART_SCS_M47)
				return true;
	return false;
}

bool ADIv5::Component::isARMv6MDWT()
{
	if (cid.ComponentClass == CID::GENERIC_IP)
		if (pid.isARM())
			if (pid.PART == ARM_PART_DWT_M00P)
				return true;
	return false;
}

bool ADIv5::Component::isARMv7MDWT()
{
	if (cid.ComponentClass == CID::GENERIC_IP)
		if (pid.isARM())
			if (pid.PART == ARM_PART_DWT_M347)
				return true;
	return false;
}

bool ADIv5::Component::isARMv6MBPU()
{
	if (cid.ComponentClass == CID::GENERIC_IP)
		if (pid.isARM())
			if (pid.PART == ARM_PART_BPU_M00P)
				return true;
	return false;
}

bool ADIv5::Component::isARMv7MFPB()
{
	if (cid.ComponentClass == CID::GENERIC_IP)
		if (pid.isARM())
			if (pid.PART == ARM_PART_FPB_M34 ||
				pid.PART == ARM_PART_FPB_M7)
				return true;
	return false;
}

bool ADIv5::Component::isRomTable()
{
	if (cid.ComponentClass == CID::ROM_TABLE)
		return true;
	return false;
}

int32_t ADIv5::Component::read()
{
	int ret;

	ret = readPid();
	if (ret != OK)
		return ret;

	ret = readCid();
	if (ret != OK)
		return ret;

	return OK;
}

void ADIv5::Component::print()
{
	_DBGPRT("    PID                 : 0x%016llx\n", pid.raw);
	_DBGPRT("      Part number       : %x\n", pid.PART);
	if (pid.JEDEC)
	{
		_DBGPRT("      Designer          : %s (JEP106 CONT.:%x, ID:%x)\n",
			getJEP106DesignerName(pid.JEP106CONTINUATION, pid.JEP106ID),
			pid.JEP106CONTINUATION, pid.JEP106ID);
	}
	_DBGPRT("      Revision          : %x\n", pid.REVISION);
	_DBGPRT("      Manufacturer Rev. : %x\n", pid.REVAND);
	_DBGPRT("      Customer modified : %x\n", pid.CMOD);
	_DBGPRT("      Size              : %dKB\n", 4 << pid.SIZE);

	_DBGPRT("    CID     : 0x%08x\n", cid.raw);
	_DBGPRT("      Class : %s\n",
		cid.ComponentClass == CID::GENERIC_VERIFICATION ? "Generic verification component" :
		cid.ComponentClass == CID::ROM_TABLE ? "ROM Table" :
		cid.ComponentClass == CID::DEBUG_COMPONENT ? "Debug component" :
		cid.ComponentClass == CID::PERIPHERAL_TEST_BLOCK ? "Peripheral Test Block" :
		cid.ComponentClass == CID::OPTIMO_DE ? "OptimoDE Data Engine SubSystem(DESS) component" :
		cid.ComponentClass == CID::GENERIC_IP ? "Generic IP component" :
		cid.ComponentClass == CID::PRIME_CELL ? "PrimeCell peripheral" : "UNKNOWN");
	_DBGPRT("    NAME    : %s\n", getName());
}
