
#include "stdafx.h"
#include "ADIv5TI.h"

ADIv5TI::ADIv5TI(ADIv5 _adi) : adi(_adi)
{
	auto _scs = adi.findARMv6MSCS();
	if (_scs.size() > 0)
	{
		scs = std::make_shared<ARMv6MSCS>(*_scs[0]);

		_DBGPRT("ARMv6-M SCS\n");
		ARMv6MSCS::CPUID cpuid;
		if (scs->readCPUID(&cpuid) == OK)
			cpuid.print();
		scs->printDEMCR();
		ARMv6MSCS::DFSR dfsr;
		if (scs->readDFSR(&dfsr) == OK)
			dfsr.print();
		scs->printDHCSR();

		// Debug state �łȂ��ƃ��W�X�^�l�͓ǂ߂Ȃ�
		scs->halt();
		scs->printRegs();
		scs->run();
	}

	auto _dwt = adi.findARMv6MDWT();
	if (_dwt.size() > 0)
	{
		dwt = std::make_shared<ARMv6MDWT>(*_dwt[0]);
		_DBGPRT("ARMv6-M DWT\n");
		dwt->printPC();
		dwt->printCtrl();
	}

	auto _v7dwt = adi.findARMv7MDWT();
	if (_v7dwt.size() > 0)
	{
		dwt = std::make_shared<ARMv7MDWT>(*_v7dwt[0]);
		_DBGPRT("ARMv6-M DWT\n");
		dwt->printPC();
		dwt->printCtrl();
	}

	auto _mem = adi.findSysmem();
	if (_mem.size() > 0)
	{
		mem = _mem[0];
	}
}

int32_t ADIv5TI::attach()
{
	if (scs)
		return scs->halt();

	return CMSISDAP_ERR_TARGET_NOT_FOUND;
}

void ADIv5TI::detach()
{
	if (scs)
		scs->run();
}

void ADIv5TI::setTargetThreadId()
{
	// TODO
}

void ADIv5TI::setCurrentPC(const uint64_t addr)
{
	// TODO
	(void)addr;
}

void ADIv5TI::resume()
{
	// continue command
	if (scs)
		scs->run();

	// [TODO] return error code
}

int32_t ADIv5TI::step(uint8_t* signal)
{
	ASSERT_RELEASE(signal != nullptr);

	*signal = 0x05;	// SIGTRAP

	if (scs)
		return scs->step();

	return CMSISDAP_ERR_TARGET_NOT_FOUND;
}

int32_t ADIv5TI::interrupt(uint8_t* signal)
{
	ASSERT_RELEASE(signal != nullptr);

	*signal = 0x05;	// SIGTRAP
	return 0;
}

int32_t ADIv5TI::setBreakPoint(BreakPointType type, uint64_t addr, uint32_t kind)
{
	return -1;	// not supported
}

int32_t ADIv5TI::unsetBreakPoint(BreakPointType type, uint64_t addr, uint32_t kind)
{
	return -1;	// not supported
}

int32_t ADIv5TI::setWatchPoint(WatchPointType type, uint64_t addr, uint32_t kind)
{
	return -1;	// not supported
}

int32_t ADIv5TI::unsetWatchPoint(WatchPointType type, uint64_t addr, uint32_t kind)
{
	return -1;	// not supported
}

int32_t ADIv5TI::readRegister(const uint32_t n, uint32_t* out)
{
	ASSERT_RELEASE(out != nullptr);

	ARMv6MSCS::REGSEL regsel = (ARMv6MSCS::REGSEL)n;

	// cpsr
	if (n == 0x19)
		regsel = ARMv6MSCS::REGSEL::xPSR;

	if (scs)
		return scs->readReg(regsel, out);

	return CMSISDAP_ERR_TARGET_NOT_FOUND;
}

int32_t ADIv5TI::readRegister(const uint32_t n, uint64_t* out)
{
	ASSERT_RELEASE(out != nullptr);
	uint32_t value;
	int32_t result = readRegister(n, &value);
	if (result == OK)
	{
		*out = value;
	}
	return result;
}

int32_t ADIv5TI::readRegister(const uint32_t n, uint64_t* out1, uint64_t* out2)
{
	ASSERT_RELEASE(out1 != nullptr && out2 != nullptr);
	uint32_t value;
	int32_t result = readRegister(n, &value);
	if (result == OK)
	{
		*out1 = value;
		*out2 = 0;
	}
	return result;
}

int32_t ADIv5TI::writeRegister(const uint32_t n, const uint32_t data)
{
	// TODO
	(void)n;
	(void)data;
	return 0;
}

int32_t ADIv5TI::writeRegister(const uint32_t n, const uint64_t data)
{
	(void)n;
	(void)data;
	return 0;
}

int32_t ADIv5TI::writeRegister(const uint32_t n, const uint64_t data1, const uint64_t data2)
{
	(void)n;
	(void)data1;
	(void)data2;
	return 0;
}

int32_t ADIv5TI::readGenericRegisters(std::vector<uint32_t>* array)
{
	ASSERT_RELEASE(array != nullptr);

	for (int i = 0; i < 16; i++)
	{
		uint32_t value;
		if (readRegister(i, &value) == OK)
		{
			array->push_back(value);
		}
		else
		{
			return -1;
		}
	}
	return 0;
}

int32_t ADIv5TI::writeGenericRegisters(const std::vector<uint32_t>& array)
{
	// TODO
	(void)array;
	return 0;
}

void ADIv5TI::readMemory(uint64_t addr, uint32_t len, std::vector<uint8_t>* array)
{
	ASSERT_RELEASE(array != nullptr);

	if (mem)
	{
		int32_t ret;
		uint32_t i = 0;
		for (; i < len / 4; i++)
		{
			uint32_t data;
			ret = mem->read((uint32_t)addr, &data);
			if (ret != OK)
				return;
			_DBGPRT("readMemory 0x%08x 0x%08x\n", (uint32_t)addr, data);
			array->push_back(data & 0xFF);
			array->push_back((data >> 8) & 0xFF);
			array->push_back((data >> 16) & 0xFF);
			array->push_back((data >> 24) & 0xFF);
			addr += 4;
		}

		len -= i * 4;
		if (len > 0)
		{
			uint32_t data;
			ret = mem->read((uint32_t)addr, &data);
			if (ret != OK)
				return;
			if (len > 0) { array->push_back((data >> 0) & 0xFF); len--; }
			if (len > 0) { array->push_back((data >> 8) & 0xFF); len--; }
			if (len > 0) { array->push_back((data >> 16) & 0xFF); len--; }
			if (len > 0) { array->push_back((data >> 24) & 0xFF); len--; }
		}
	}
}

uint8_t ADIv5TI::writeMemory(uint64_t addr, uint32_t len, const std::vector<uint8_t>& array)
{
	// TODO
	(void)addr;
	(void)len;
	(void)array;
	return 0;
}

int32_t ADIv5TI::monitor(const std::string command, std::string* output)
{
	ASSERT_RELEASE(output != nullptr);

	printf("monitor [%s]\n", command.c_str());

	// TODO
	(void)command;
	(void)output;
	return 0;
}