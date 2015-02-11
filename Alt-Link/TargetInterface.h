﻿
#pragma once

#include <cstdint>
#include <vector>

class TargetInterface
{
public:
	virtual void setTargetThreadId() = 0;
	virtual void setCurrentPC(const uint64_t addr) = 0;

	virtual void resume() = 0;
	virtual int32_t step(uint8_t* signal) = 0;
	virtual int32_t interrupt(uint8_t* signal) = 0;

	enum BreakPointType
	{
		MEMORY = 0,
		HARDWARE = 1
	};
	virtual int32_t setBreakPoint(BreakPointType type, uint64_t addr, uint32_t kind) = 0;
	virtual int32_t unsetBreakPoint(BreakPointType type, uint64_t addr, uint32_t kind) = 0;

	enum WatchPointType
	{
		WRITE = 2,
		READ = 3,
		ACCESS = 4
	};
	virtual int32_t setWatchPoint(WatchPointType type, uint64_t addr, uint32_t kind) = 0;
	virtual int32_t unsetWatchPoint(WatchPointType type, uint64_t addr, uint32_t kind) = 0;

	virtual int32_t readRegister(const uint32_t n, uint32_t* out) = 0;
	virtual int32_t readRegister(const uint32_t n, uint64_t* out) = 0;
	virtual int32_t readRegister(const uint32_t n, uint64_t* out1, uint64_t* out2) = 0;	// 128-bit
	virtual int32_t writeRegister(const uint32_t n, const uint32_t data) = 0;
	virtual int32_t writeRegister(const uint32_t n, const uint64_t data) = 0;
	virtual int32_t writeRegister(const uint32_t n, const uint64_t data1, const uint64_t data2) = 0; // 128-bit
	virtual int32_t readGenericRegisters(std::vector<uint32_t>* array) = 0;
	virtual int32_t writeGenericRegisters(const std::vector<uint32_t>& array) = 0;

	virtual void readMemory(uint64_t addr, uint32_t len, std::vector<uint8_t>* array) = 0;
	virtual uint8_t writeMemory(uint64_t addr, uint32_t len, const std::vector<uint8_t>& array) = 0;
};