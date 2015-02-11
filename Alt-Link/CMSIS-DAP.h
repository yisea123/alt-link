
#pragma once

#include <cstdint>
#include <vector>

#if defined(_WIN32)
#pragma comment(lib, "setupapi.lib")
#if defined(_DEBUG)
#pragma comment(lib, "hidapid.lib")
#else
#pragma comment(lib, "hidapi.lib")
#endif
#endif
#include <hidapi.h>

#include "TargetInterface.h"

/* FIXME error code */
#define CMSISDAP_OK								0
#define CMSISDAP_ERR_FATAL						1
#define CMSISDAP_ERR_INVALID_ARGUMENT			2
#define CMSISDAP_ERR_NO_MEMORY					3
#define CMSISDAP_ERR_DAP_RES					4
#define CMSISDAP_ERR_ACKFAULT					5
#define CMSISDAP_ERR_INVALID_TX_LEN				6
#define CMSISDAP_ERR_USBHID_INIT				7
#define CMSISDAP_ERR_USBHID_OPEN				8
#define CMSISDAP_ERR_USBHID_NOT_FOUND_DEVICE	9
#define CMSISDAP_ERR_USBHID_WRITE				10
#define CMSISDAP_ERR_USBHID_TIMEOUT				11
#define CMSISDAP_ERR_USBHID_EXIT				12

class CMSISDAP : public TargetInterface
{
public:
	virtual void setTargetThreadId();
	virtual void setCurrentPC(const uint64_t addr);

	virtual void resume();
	virtual int32_t step(uint8_t* signal);
	virtual int32_t interrupt(uint8_t* signal);

	virtual int32_t setBreakPoint(BreakPointType type, uint64_t addr, uint32_t kind);
	virtual int32_t unsetBreakPoint(BreakPointType type, uint64_t addr, uint32_t kind);

	virtual int32_t setWatchPoint(WatchPointType type, uint64_t addr, uint32_t kind);
	virtual int32_t unsetWatchPoint(WatchPointType type, uint64_t addr, uint32_t kind);

	virtual int32_t readRegister(const uint32_t n, uint32_t* out);
	virtual int32_t readRegister(const uint32_t n, uint64_t* out);
	virtual int32_t readRegister(const uint32_t n, uint64_t* out1, uint64_t* out2);	// 128-bit
	virtual int32_t writeRegister(const uint32_t n, const uint32_t data);
	virtual int32_t writeRegister(const uint32_t n, const uint64_t data);
	virtual int32_t writeRegister(const uint32_t n, const uint64_t data1, const uint64_t data2); // 128-bit
	virtual int32_t readGenericRegisters(std::vector<uint32_t>* array);
	virtual int32_t writeGenericRegisters(const std::vector<uint32_t>& array);

	virtual void readMemory(uint64_t addr, uint32_t len, std::vector<uint8_t>* array);
	virtual uint8_t writeMemory(uint64_t addr, uint32_t len, const std::vector<uint8_t>& array);

	int32_t CMSISDAP::initialize(void);
	int32_t CMSISDAP::finalize(void);
	int32_t CMSISDAP::resetSw(void);
	int32_t CMSISDAP::resetHw(void);
	int32_t CMSISDAP::setSpeed(uint32_t speed);

private:
	enum CMD {
		CMD_INFO = 0x00,
		CMD_LED = 0x01,
		CMD_CONNECT = 0x02,
		CMD_DISCONNECT = 0x03,
		CMD_TX_CONF = 0x04,
		CMD_TX = 0x05,
		CMD_TX_BLOCK = 0x06,
		CMD_TX_ABORT = 0x07,
		CMD_WRITE_ABORT = 0x08,
		CMD_DELAY = 0x09,
		CMD_RESET_TARGET = 0x0A,
		CMD_SWJ_PINS = 0x10,
		CMD_SWJ_CLOCK = 0x11,
		CMD_SWJ_SEQ = 0x12,
		CMD_SWD_CONF = 0x13,
		CMD_JTAG_SEQ = 0x14,
		CMD_JTAG_CONFIGURE = 0x15,
		CMD_JTAG_IDCODE = 0x16,
	};

	enum INFO_ID {
		INFO_ID_VID = 0x00,         /* string */
		INFO_ID_PID = 0x02,         /* string */
		INFO_ID_SERNUM = 0x03,      /* string */
		INFO_ID_FW_VER = 0x04,      /* string */
		INFO_ID_TD_VEND = 0x05,     /* string */
		INFO_ID_TD_NAME = 0x06,     /* string */
		INFO_ID_CAPABILITIES = 0xf0,/* byte */
		INFO_ID_PKT_CNT = 0xfe,     /* byte */
		INFO_ID_PKT_SZ = 0xff,      /* short */
	};

	hid_device *hidHandle;
	uint8_t *packetBuf;
	char *fwver;
	char *name;
	char *vendor;
	uint32_t idcode;
	uint32_t ap_bank_value;
	uint16_t packetBufSize;
	uint16_t packetMaxCount;
	uint16_t pid;
	uint16_t vid;
	uint8_t caps;

	int32_t CMSISDAP::usbOpen(void);
	int32_t CMSISDAP::usbClose(void);
	int32_t CMSISDAP::usbTx(uint32_t txlen);
	int32_t CMSISDAP::cmdInfoCapabilities(void);
	int32_t CMSISDAP::cmdLed(uint8_t led, uint8_t on);
	int32_t CMSISDAP::cmdConnect(void);
	int32_t CMSISDAP::cmdDisconnect(void);
	int32_t CMSISDAP::cmdTxConf(uint8_t idle, uint16_t delay, uint16_t retry);
	int32_t CMSISDAP::cmdInfoFwVer(void);
	int32_t CMSISDAP::cmdInfoVendor(void);
	int32_t CMSISDAP::cmdInfoName(void);
	int32_t CMSISDAP::cmdInfoPacketSize(void);
	int32_t CMSISDAP::cmdInfoPacketCount(void);
	int32_t CMSISDAP::getStatus(void);
	int32_t CMSISDAP::change2Swd(void);
	int32_t CMSISDAP::resetLink(void);
	int32_t CMSISDAP::cmdSwjPins(uint8_t isLevelHigh, uint8_t pin, uint32_t delay, uint8_t *input);
	int32_t CMSISDAP::cmdSwjClock(uint32_t clock);
	int32_t CMSISDAP::cmdSwdConf(uint8_t cfg);
	int32_t CMSISDAP::dpRead(uint32_t reg, uint32_t *data);
	int32_t CMSISDAP::dpWrite(uint32_t reg, uint32_t val);
};