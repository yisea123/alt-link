
#include "stdafx.h"
#include "common.h"

#include "CMSIS-DAP.h"

#if defined(_WIN32)
#undef strdup
#define strdup _strdup
#endif

#define USE_USB_TX_DBG 0

/*
 * based on CMSIS-DAP Beta 0.01.
 * https://silver.arm.com/browse/CMSISDAP
 *
 * CoreSight Components Technical Reference Manual
 * http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.ddi0432cj/Bcgbfdhc.html
 */

#define _DBGPRT printf
#define _ERRPRT printf

#define _USB_HID_REPORT_NUM 0x00

#define _CMSISDAP_DEFAULT_PACKET_SIZE (64 + 1) /* 64 bytes + 1 byte(hid report id) */
#define _CMSISDAP_MAX_CLOCK (10 * 1000 * 1000) /* Hz */
#define _CMSISDAP_USB_TIMEOUT 1000             /* ms */

static inline uint32_t buf2LE32(const uint8_t *buf)
{
	return (uint32_t)(buf[0] | buf[1] << 8 | buf[2] << 16 | buf[3] << 24);
}

#define _INFO_CAPSSWD 0x01
#define _INFO_CAPSJTAG 0x02
#define _INFO_CAPSBOTH (_INFO_CAPSSWD | _INFO_CAPSJTAG)

/* CMD_LED */
#define _LED_CONNECT 0
#define _LED_RUNNING 1

/* CMD_CONNECT */
#define _CONNECT_IF_DEFAULT 0x00
#define _CONNECT_IF_SWD 0x01
#define _CONNECT_IF_JTAG 0x02

/* DAP Status Code */
#define _DAP_RES_OK 0
#define _DAP_RES_ERR 0xFF

#define _PIN_SWCLK (1 << 0)  /* SWCLK/TCK */
#define _PIN_SWDIO (1 << 1)  /* SWDIO/TMS */
#define _PIN_TDI (1 << 2)    /* TDI */
#define _PIN_TDO (1 << 3)    /* TDO */
#define _PIN_nTRST (1 << 5)  /* nTRST */
#define _PIN_nRESET (1 << 7) /* nRESET */

#define _TX_RES_OK 0x1
#define _TX_RES_WAIT 0x2
#define _TX_RES_SWD_ERROR 0x4
#define _TX_RES_VALUE_MISMATCH 0x8

#define AP_ABORT_DAPABORT 0x01     /* generate a DAP abort */
#define AP_ABORT_STK_CMP_CLR 0x02  /* clear STICKYCMP sticky compare flag */
#define AP_ABORT_STK_ERR_CLR 0x04  /* clear STICKYERR sticky error flag */
#define AP_ABORT_WD_ERR_CLR 0x08   /* clear WDATAERR write data error flag */
#define AP_ABORT_ORUN_ERR_CLR 0x10 /* clear STICKYORUN overrun error flag */

#define CMSIS_CMD_DP (0 << 0)         /* set only for AP access */
#define CMSIS_CMD_AP (1 << 0)         /* set only for AP access */
#define CMSIS_CMD_READ (1 << 1)       /* set only for read access */
#define CMSIS_CMD_WRITE (0 << 1)      /* set only for read access */
#define CMSIS_CMD_A32(n) ((n) & 0x0C) /* bits A[3:2] of register addr */
#define CMSIS_CMD_VAL_MATCH (1 << 4)  /* Value Match */
#define CMSIS_CMD_MATCH_MSK (1 << 5)  /* Match Mask */

/* three-bit ACK values for SWD access (sent LSB first) */
#define TX_ACK_OK 0x1
#define TX_ACK_WAIT 0x2
#define TX_ACK_FAULT 0x4

#define DPAP_WRITE 0
#define DPAP_READ 1

#define BANK_REG(bank, reg) (((bank) << 4) | (reg))

/* A[3:0] for DP registers; A[1:0] are always zero.
 * - JTAG accesses all of these via JTAG_DP_DPACC, except for
 *   IDCODE (JTAG_DP_IDCODE) and ABORT (JTAG_DP_ABORT).
 * - SWD accesses these directly, sometimes needing SELECT.CTRLSEL
 */
#define DP_IDCODE BANK_REG(0x0, 0x0)    /* SWD: read */
#define DP_ABORT BANK_REG(0x0, 0x0)     /* SWD: write */
#define DP_CTRL_STAT BANK_REG(0x0, 0x4) /* r/w */
#define DP_RESEND BANK_REG(0x0, 0x8)    /* SWD: read */
#define DP_SELECT BANK_REG(0x0, 0x8)    /* JTAG: r/w; SWD: write */
#define DP_RDBUFF BANK_REG(0x0, 0xC)    /* read-only */
#define DP_WCR BANK_REG(0x1, 0x4)       /* SWD: r/w */

#define WCR_TO_TRN(wcr) ((uint32_t)(1 + (3 & ((wcr)) >> 8))) /* 1..4 clocks */
#define WCR_TO_PRESCALE(wcr) ((uint32_t)(7 & ((wcr))))       /* impl defined */

/* Fields of the DP's AP ABORT register */
#define DAPABORT (1UL << 0)
#define STKCMPCLR (1UL << 1)  /* SWD-only */
#define STKERRCLR (1UL << 2)  /* SWD-only */
#define WDERRCLR (1UL << 3)   /* SWD-only */
#define ORUNERRCLR (1UL << 4) /* SWD-only */

/* Fields of the DP's CTRL/STAT register */
#define CORUNDETECT (1UL << 0)
#define SSTICKYORUN (1UL << 1)
/* 3:2 - transaction mode (e.g. pushed compare) */
#define SSTICKYCMP (1UL << 4)
#define SSTICKYERR (1UL << 5)
#define READOK (1UL << 6)   /* SWD-only */
#define WDATAERR (1UL << 7) /* SWD-only */
/* 11:8 - mask lanes for pushed compare or verify ops */
/* 21:12 - transaction counter */
#define CDBGRSTREQ (1UL << 26)
#define CDBGRSTACK (1UL << 27)
#define CDBGPWRUPREQ (1UL << 28)
#define CDBGPWRUPACK (1UL << 29)
#define CSYSPWRUPREQ (1UL << 30)
#define CSYSPWRUPACK (1UL << 31)

/* MEM-AP register addresses */
/* TODO: rename as MEM_AP_REG_* */
#define AP_REG_CSW 0x00
#define AP_REG_TAR 0x04
#define AP_REG_DRW 0x0C
#define AP_REG_BD0 0x10
#define AP_REG_BD1 0x14
#define AP_REG_BD2 0x18
#define AP_REG_BD3 0x1C
#define AP_REG_CFG 0xF4 /* big endian? */
#define AP_REG_BASE 0xF8

/* Generic AP register address */
#define AP_REG_IDR 0xFC

/* Fields of the MEM-AP's CSW register */
#define CSW_8BIT 0
#define CSW_16BIT 1
#define CSW_32BIT 2
#define CSW_ADDRINC_MASK (3UL << 4)
#define CSW_ADDRINC_OFF 0UL
#define CSW_ADDRINC_SINGLE (1UL << 4)
#define CSW_ADDRINC_PACKED (2UL << 4)
#define CSW_DEVICE_EN (1UL << 6)
#define CSW_TRIN_PROG (1UL << 7)
#define CSW_SPIDEN (1UL << 23)
/* 30:24 - implementation-defined! */
#define CSW_HPROT (1UL << 25)        /* ? */
#define CSW_MASTER_DEBUG (1UL << 29) /* ? */
#define CSW_SPROT (1UL << 30)
#define CSW_DBGSWENABLE (1UL << 31)

int32_t CMSISDAP::usbOpen(void)
{
	struct hid_device_info *info;
	struct hid_device_info *infoCur;
	uint16_t usbVID = 0;
	uint16_t usbPID = 0;

	packetBufSize = _CMSISDAP_DEFAULT_PACKET_SIZE;
	infoCur = info = hid_enumerate(0x0, 0x0);
	while (NULL != infoCur) {
#if 0
		printf("type: %04hx %04hx\npath: %s\nserial_number: %ls\n", infoCur->vendor_id, infoCur->product_id, infoCur->path, infoCur->serial_number);
		printf("Manu    : %ls\n", infoCur->manufacturer_string);
		printf("Product : %ls\n", infoCur->product_string);
#endif
		if (infoCur->product_string != NULL &&
			wcsstr(infoCur->product_string, L"CMSIS-DAP"))
		{
			break;
		}
		infoCur = infoCur->next;
	}

	if (infoCur != NULL)
	{
		usbVID = infoCur->vendor_id;
		usbPID = infoCur->product_id;
	}

	if (info != NULL)
	{
		hid_free_enumeration(info);
	}

	if (usbVID == 0 && usbPID == 0)
	{
		return CMSISDAP_ERR_USBHID_NOT_FOUND_DEVICE;
	}

	if (hid_init() != 0)
	{
		return CMSISDAP_ERR_USBHID_INIT;
	}

	hidHandle = hid_open(usbVID, usbPID, NULL);
	if (hidHandle == NULL)
	{
		return CMSISDAP_ERR_USBHID_OPEN;
	}

	packetBufSize = packetBufSize;
	vid = usbVID;
	pid = usbPID;

	packetBuf = (uint8_t *)malloc(packetBufSize);
	if (packetBuf == NULL)
	{
		return CMSISDAP_ERR_NO_MEMORY;
	}

	return CMSISDAP_OK;
}

int32_t CMSISDAP::usbClose(void)
{
	(void)hid_close(hidHandle);
	if (hid_exit() != 0)
	{
		return CMSISDAP_ERR_USBHID_EXIT;
	}
	return CMSISDAP_OK;
}

int32_t CMSISDAP::usbTx(uint32_t txlen)
{
	int ret;
#if USE_USB_TX_DBG
	uint8_t *buf = packetBuf;
#endif /* USE_USB_TX_DBG */

	if (packetBufSize - 1u < txlen) {
		return CMSISDAP_ERR_INVALID_TX_LEN;
	}

	memset(packetBuf + txlen, 0, packetBufSize - 1 - txlen);

#if USE_USB_TX_DBG
	_DBGPRT("A %02x %02x %02x %02x %02x %02x %02x %02x\n",
		buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7], buf[8]);
#endif /* USE_USB_TX_DBG */
	ret = hid_write(hidHandle, packetBuf, packetBufSize);
	if (ret == -1) {
		return CMSISDAP_ERR_USBHID_WRITE;
	}

	ret = hid_read_timeout(hidHandle, packetBuf, packetBufSize, _CMSISDAP_USB_TIMEOUT);
	if (ret == -1 || ret == 0) {
		return CMSISDAP_ERR_USBHID_TIMEOUT;
	}
#if USE_USB_TX_DBG
	_DBGPRT("B %02x %02x %02x %02x %02x %02x %02x %02x\n",
		buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
#endif /* USE_USB_TX_DBG */

	return CMSISDAP_OK;
}

int32_t CMSISDAP::cmdInfoCapabilities(void)
{
	int ret;
	uint32_t idx = 0;

	packetBuf[idx++] = _USB_HID_REPORT_NUM;
	packetBuf[idx++] = CMD_INFO;
	packetBuf[idx++] = INFO_ID_CAPABILITIES;
	ret = usbTx(idx);
	if (ret != CMSISDAP_OK) {
		_DBGPRT("err ret=%08x %s %s %d\n", ret, __FUNCTION__, __FILE__, __LINE__);
		return ret;
	}

	if (packetBuf[1] != 1) {
		return CMSISDAP_ERR_DAP_RES;
	}

	caps = packetBuf[2];

	return CMSISDAP_OK;
}

int32_t CMSISDAP::cmdLed(uint8_t led, uint8_t on)
{
	int ret;
	uint32_t idx = 0;
	packetBuf[idx++] = _USB_HID_REPORT_NUM;
	packetBuf[idx++] = CMD_LED;
	packetBuf[idx++] = led;
	packetBuf[idx++] = !!(on);
	ret = usbTx(idx);
	if (ret != CMSISDAP_OK) {
		_DBGPRT("err ret=%08x %s %s %d\n", ret, __FUNCTION__, __FILE__, __LINE__);
	}
	if (packetBuf[1] != _DAP_RES_OK) {
		ret = CMSISDAP_ERR_DAP_RES;
		_DBGPRT("err ret=%08x %s %s %d\n", ret, __FUNCTION__, __FILE__, __LINE__);
	}

	return ret;
}

int32_t CMSISDAP::cmdConnect(void)
{
	int ret;
	uint32_t idx = 0;

	packetBuf[idx++] = _USB_HID_REPORT_NUM;
	packetBuf[idx++] = CMD_CONNECT;
	packetBuf[idx++] = _CONNECT_IF_SWD;
	ret = usbTx(idx);
	if (ret != CMSISDAP_OK) {
		_DBGPRT("err ret=%08x %s %s %d\n", ret, __FUNCTION__, __FILE__, __LINE__);
		return ret;
	}

	if (packetBuf[1] != _CONNECT_IF_SWD) {
		return CMSISDAP_ERR_FATAL;
	}

	return CMSISDAP_OK;
}

int32_t CMSISDAP::cmdDisconnect(void)
{
	int ret;
	uint32_t idx = 0;

	packetBuf[idx++] = _USB_HID_REPORT_NUM;
	packetBuf[idx++] = CMD_DISCONNECT;
	ret = usbTx(idx);
	if (ret != CMSISDAP_OK)
	{
		_DBGPRT("err ret=%08x %s %s %d\n", ret, __FUNCTION__, __FILE__, __LINE__);
		return ret;
	}
	if (packetBuf[1] != _DAP_RES_OK)
	{
		ret = CMSISDAP_ERR_DAP_RES;
		_DBGPRT("err ret=%08x %s %s %d\n", ret, __FUNCTION__, __FILE__, __LINE__);
	}

	return ret;
}

int32_t CMSISDAP::cmdTxConf(uint8_t idle, uint16_t delay, uint16_t retry)
{
	int ret;
	uint32_t idx = 0;

	packetBuf[idx++] = _USB_HID_REPORT_NUM;
	packetBuf[idx++] = CMD_TX_CONF;
	packetBuf[idx++] = idle;
	packetBuf[idx++] = delay & 0xff;
	packetBuf[idx++] = (delay >> 8) & 0xff;
	packetBuf[idx++] = retry & 0xff;
	packetBuf[idx++] = (retry >> 8) & 0xff;
	ret = usbTx(idx);
	if (ret != CMSISDAP_OK)
	{
		_DBGPRT("err ret=%08x %s %s %d\n", ret, __FUNCTION__, __FILE__, __LINE__);
		return ret;
	}
	if (packetBuf[1] != _DAP_RES_OK)
	{
		ret = CMSISDAP_ERR_DAP_RES;
		_DBGPRT("err ret=%08x %s %s %d\n", ret, __FUNCTION__, __FILE__, __LINE__);
	}

	return ret;
}

int32_t CMSISDAP::cmdInfoFwVer(void)
{
	int ret;
	uint32_t idx = 0;

	packetBuf[idx++] = _USB_HID_REPORT_NUM;
	packetBuf[idx++] = CMD_INFO;
	packetBuf[idx++] = INFO_ID_FW_VER;
	ret = usbTx(idx);
	if (ret != CMSISDAP_OK) {
		_DBGPRT("err ret=%08x %s %s %d\n", ret, __FUNCTION__, __FILE__, __LINE__);
		return ret;
	}

	if (packetBuf[1] > 0) {
		fwver = strdup((const char *)&(packetBuf[2]));
	} else {
		fwver = NULL;
	}

	return CMSISDAP_OK;
}

int32_t CMSISDAP::cmdInfoVendor(void)
{
	int ret;
	uint32_t idx = 0;

	packetBuf[idx++] = _USB_HID_REPORT_NUM;
	packetBuf[idx++] = CMD_INFO;
	packetBuf[idx++] = INFO_ID_TD_VEND;
	ret = usbTx(idx);
	if (ret != CMSISDAP_OK) {
		_DBGPRT("err ret=%08x %s %s %d\n", ret, __FUNCTION__, __FILE__, __LINE__);
		return ret;
	}

	if (packetBuf[1] > 0)
	{
		vendor = strdup((const char *)&(packetBuf[2]));
	}
   	else
	{
		vendor = NULL;
	}

	return CMSISDAP_OK;
}

int32_t CMSISDAP::cmdInfoName(void)
{
	int ret;
	uint32_t idx = 0;

	packetBuf[idx++] = _USB_HID_REPORT_NUM;
	packetBuf[idx++] = CMD_INFO;
	packetBuf[idx++] = INFO_ID_TD_NAME;
	ret = usbTx(idx);
	if (ret != CMSISDAP_OK) {
		_DBGPRT("err ret=%08x %s %s %d\n", ret, __FUNCTION__, __FILE__, __LINE__);
		return ret;
	}

	if (packetBuf[1] > 0)
	{
		name = strdup((const char *)&(packetBuf[2]));
	}
	else
	{
		name = NULL;
	}

	return CMSISDAP_OK;
}

int32_t CMSISDAP::cmdInfoPacketSize(void)
{
	int ret;
	uint16_t size;
	uint32_t idx = 0;

	packetBuf[idx++] = _USB_HID_REPORT_NUM;
	packetBuf[idx++] = CMD_INFO;
	packetBuf[idx++] = INFO_ID_PKT_SZ;
	ret = usbTx(idx);
	if (ret != CMSISDAP_OK) {
		_DBGPRT("err ret=%08x %s %s %d\n", ret, __FUNCTION__, __FILE__, __LINE__);
		return ret;
	}

	if (packetBuf[1] != 2) {
		ret = CMSISDAP_ERR_DAP_RES;
		_DBGPRT("err ret=%08x %s %s %d\n", ret, __FUNCTION__, __FILE__, __LINE__);
		return ret;
	}
	size = packetBuf[2] + (packetBuf[3] << 8);

	if (packetBufSize != size + 1) {
		// 現在のバッファサイズと異なる場合、realloc
		uint8_t *tmp;
		/* reallocate buffer */
		packetBufSize = size + 1;
		tmp = (uint8_t*)realloc(packetBuf, packetBufSize);
		if (tmp == NULL) {
			free(packetBuf);
			return CMSISDAP_ERR_NO_MEMORY;
		}
		packetBuf = tmp;
	}
	return CMSISDAP_OK;
}

int32_t CMSISDAP::cmdInfoPacketCount(void)
{
	int ret;
	uint32_t idx = 0;

	packetBuf[idx++] = _USB_HID_REPORT_NUM;
	packetBuf[idx++] = CMD_INFO;
	packetBuf[idx++] = INFO_ID_PKT_CNT;
	ret = usbTx(idx);
	if (ret != CMSISDAP_OK) {
		_DBGPRT("err ret=%08x %s %s %d\n", ret, __FUNCTION__, __FILE__, __LINE__);
		return ret;
	}

	if (packetBuf[1] != 1) { /* resは1byte */
		ret = CMSISDAP_ERR_DAP_RES;
		_DBGPRT("err ret=%08x %s %s %d\n", ret, __FUNCTION__, __FILE__, __LINE__);
		return ret;
	}
	packetMaxCount = packetBuf[2];
	return CMSISDAP_OK;
}

int32_t CMSISDAP::getStatus(void)
{
	int ret;
	uint8_t d;

	ret = cmdSwjPins(0, _PIN_SWCLK, 0, &d);
	if (ret != CMSISDAP_OK) {
		_DBGPRT("err ret=%08x %s %s %d\n", ret, __FUNCTION__, __FILE__, __LINE__);
		return ret;
	}

	_DBGPRT("SWCLK:%d SWDIO:%d TDI:%d TDO:%d !TRST:%d !RESET:%d\n", (d & _PIN_SWCLK) ? 1 : 0, (d & _PIN_SWDIO) ? 1 : 0,
	        (d & _PIN_TDI) ? 1 : 0, (d & _PIN_TDO) ? 1 : 0, (d & _PIN_nTRST) ? 1 : 0, (d & _PIN_nRESET) ? 1 : 0);

	return CMSISDAP_OK;
}

int32_t CMSISDAP::change2Swd(void)
{
	int ret;
	uint32_t idx = 0;

	packetBuf[idx++] = _USB_HID_REPORT_NUM;
	packetBuf[idx++] = CMD_SWJ_SEQ;
	packetBuf[idx++] = 2 * 8;
	packetBuf[idx++] = 0x9e;
	packetBuf[idx++] = 0xe7;
	ret = usbTx(idx);
	if (ret != CMSISDAP_OK) {
		_DBGPRT("err ret=%08x %s %s %d\n", ret, __FUNCTION__, __FILE__, __LINE__);
	}

	idx = 0;
	packetBuf[idx++] = _USB_HID_REPORT_NUM;
	packetBuf[idx++] = CMD_SWJ_SEQ;
	packetBuf[idx++] = 7 * 8;
	packetBuf[idx++] = 0xff;
	packetBuf[idx++] = 0xff;
	packetBuf[idx++] = 0xff;
	packetBuf[idx++] = 0xff;
	packetBuf[idx++] = 0xff;
	packetBuf[idx++] = 0xff;
	packetBuf[idx++] = 0xff;
	ret = usbTx(idx);
	if (ret != CMSISDAP_OK) {
		_DBGPRT("err ret=%08x %s %s %d\n", ret, __FUNCTION__, __FILE__, __LINE__);
		return ret;
	}

	/* 16 cycle idle period */
	idx = 0;
	packetBuf[idx++] = _USB_HID_REPORT_NUM;
	packetBuf[idx++] = CMD_SWJ_SEQ;
	packetBuf[idx++] = 2 * 8;
	packetBuf[idx++] = 0x00;
	ret = usbTx(idx);
	if (ret != CMSISDAP_OK) {
		_DBGPRT("err ret=%08x %s %s %d\n", ret, __FUNCTION__, __FILE__, __LINE__);
		return ret;
	}

	return ret;
}

int32_t CMSISDAP::resetLink(void)
{
	int ret;
	uint32_t idx = 0;

	/* da14 は未実装 */
	packetBuf[idx++] = _USB_HID_REPORT_NUM;
	packetBuf[idx++] = CMD_RESET_TARGET;
	ret = usbTx(idx);
	if (ret != CMSISDAP_OK) {
		_DBGPRT("err ret=%08x %s %s %d\n", ret, __FUNCTION__, __FILE__, __LINE__);
		return ret;
	}
	_DBGPRT("Target Reset Res: Status:%02x Execute:%s\n", packetBuf[1], packetBuf[2] == 0x1 ? "OK" : "no impl");

	idx = 0;
	packetBuf[idx++] = _USB_HID_REPORT_NUM;
	packetBuf[idx++] = CMD_WRITE_ABORT;
	packetBuf[idx++] = 0x00; /* DAP Index, ignored in the swd. */
	packetBuf[idx++] = AP_ABORT_STK_CMP_CLR | AP_ABORT_WD_ERR_CLR | AP_ABORT_ORUN_ERR_CLR | AP_ABORT_STK_ERR_CLR;
	packetBuf[idx++] = 0x00; /* SBZ */
	packetBuf[idx++] = 0x00; /* SBZ */
	packetBuf[idx++] = 0x00; /* SBZ */
	ret = usbTx(idx);
	if (ret != CMSISDAP_OK) {
		_DBGPRT("err ret=%08x %s %s %d\n", ret, __FUNCTION__, __FILE__, __LINE__);
	}

	if (packetBuf[1] != _DAP_RES_OK) {
		ret = CMSISDAP_ERR_DAP_RES;
		_DBGPRT("err ret=%08x %s %s %d\n", ret, __FUNCTION__, __FILE__, __LINE__);
	}

	return ret;
}

int32_t CMSISDAP::cmdSwjPins(uint8_t isLevelHigh, uint8_t pin, uint32_t delay, uint8_t *input)
{
	int ret;
	uint32_t idx = 0;
	packetBuf[idx++] = _USB_HID_REPORT_NUM;
	packetBuf[idx++] = CMD_SWJ_PINS;
	packetBuf[idx++] = !!(isLevelHigh);
	packetBuf[idx++] = pin;
	packetBuf[idx++] = delay & 0xff;
	packetBuf[idx++] = (delay >> 8) & 0xff;
	packetBuf[idx++] = (delay >> 16) & 0xff;
	packetBuf[idx++] = (delay >> 24) & 0xff;
	ret = usbTx(idx);
	if (ret != CMSISDAP_OK)
	{
		_DBGPRT("err ret=%08x %s %s %d\n", ret, __FUNCTION__, __FILE__, __LINE__);
		return ret;
	}
	if (input != NULL)
	{
		*input = packetBuf[1];
	}

	return ret;
}

int32_t CMSISDAP::cmdSwjClock(uint32_t clock)
{
	int ret;
	uint32_t idx = 0;

	packetBuf[idx++] = _USB_HID_REPORT_NUM;
	packetBuf[idx++] = CMD_SWJ_CLOCK;
	packetBuf[idx++] = clock & 0xff;
	packetBuf[idx++] = (clock >> 8) & 0xff;
	packetBuf[idx++] = (clock >> 16) & 0xff;
	packetBuf[idx++] = (clock >> 24) & 0xff;
	ret = usbTx(idx);
	if (ret != CMSISDAP_OK)
	{
		_DBGPRT("err ret=%08x %s %s %d\n", ret, __FUNCTION__, __FILE__, __LINE__);
		return ret;
	}
	if (packetBuf[1] != _DAP_RES_OK)
	{
		ret = CMSISDAP_ERR_DAP_RES;
		_DBGPRT("err ret=%08x %s %s %d\n", ret, __FUNCTION__, __FILE__, __LINE__);
	}

	return ret;
}

int32_t CMSISDAP::cmdSwdConf(uint8_t cfg)
{
	int ret;
	uint32_t idx = 0;

	packetBuf[idx++] = _USB_HID_REPORT_NUM;
	packetBuf[idx++] = CMD_SWD_CONF;
	packetBuf[idx++] = cfg;
	ret = usbTx(idx);
	if (ret != CMSISDAP_OK)
	{
		_DBGPRT("err ret=%08x %s %s %d\n", ret, __FUNCTION__, __FILE__, __LINE__);
		return ret;
	}
	if (packetBuf[1] != _DAP_RES_OK)
	{
		ret = CMSISDAP_ERR_DAP_RES;
		_DBGPRT("err ret=%08x %s %s %d\n", ret, __FUNCTION__, __FILE__, __LINE__);
	}

	return ret;
}

int32_t CMSISDAP::initialize(void)
{
	int32_t ret;
	ret = usbOpen();
	if (ret != CMSISDAP_OK)
	{
		return ret;
	}

	ret = cmdInfoCapabilities();
	if (ret != CMSISDAP_OK)
	{
		return ret;
	}

	ret = cmdLed(_LED_RUNNING, 0);
	if (ret != CMSISDAP_OK)
	{
		return ret;
	}

	ret = cmdLed( _LED_CONNECT, 0);
	if (ret != CMSISDAP_OK)
	{
		return ret;
	}

	ret = cmdLed(_LED_CONNECT, 1);
	if (ret != CMSISDAP_OK)
	{
		return ret;
	}

	ret = cmdConnect();
	if (ret != CMSISDAP_OK)
	{
		return ret;
	}

	ret = cmdInfoFwVer();
	if (ret != CMSISDAP_OK)
	{
		return ret;
	}

	ret = cmdInfoVendor();
	if (ret != CMSISDAP_OK)
	{
		return ret;
	}

	ret = cmdInfoName();
	if (ret != CMSISDAP_OK)
	{
		return ret;
	}

	ret = cmdInfoPacketSize();
	if (ret != CMSISDAP_OK) {
		return ret;
	}

	ret = cmdInfoPacketCount();
	if (ret != CMSISDAP_OK) {
		return ret;
	}

	ret = getStatus();
	if (ret != CMSISDAP_OK) {
		return ret;
	}

	ret = cmdSwjClock(100 * 1000); /* 100kHz */
	if (ret != CMSISDAP_OK) {
		return ret;
	}

	ret = cmdTxConf(0, 64, 0);
	if (ret != CMSISDAP_OK) {
		return ret;
	}
	ret = cmdSwdConf(0x00);
	if (ret != CMSISDAP_OK) {
		return ret;
	}

	ret = cmdLed(_LED_RUNNING, 1);
	if (ret != CMSISDAP_OK) {
		return ret;
	}

	/* magic packetを送り、SWDへ移行 */
	ret = change2Swd();
	if (ret != CMSISDAP_OK) {
		return ret;
	}

	/* IDCODE をDpReadしてみる */
	ret = dpRead(DP_IDCODE, &idcode);
	if (ret != CMSISDAP_OK) {
		_ERRPRT("Can not read the idcode of target device.\n");
		return ret;
	}

	ret = resetLink();
	if (ret != CMSISDAP_OK) {
		(void)cmdLed(_LED_RUNNING, 0);
		return ret;
	}

	ret = cmdLed(_LED_RUNNING, 0);
	if (ret != CMSISDAP_OK) {
		return ret;
	}

	_DBGPRT("Init OK.\n");
	_DBGPRT("  F/W Version : %s\n", fwver == NULL ? "none" : fwver);
	_DBGPRT("  Packet Size : %u\n", packetBufSize);
	_DBGPRT("  Packet Cnt  : %u\n", packetMaxCount);
	_DBGPRT("  Caps        : 0x%02x\n", caps);
	_DBGPRT("  Vendor Name : %s\n", vendor == NULL ? "none" : vendor);
	_DBGPRT("  Name        : %s\n", name == NULL ? "none" : name);
	_DBGPRT("  USB PID     : 0x%04x\n", pid);
	_DBGPRT("  USB VID     : 0x%04x\n", vid);
	_DBGPRT("  IDCODE      : 0x%08x\n", idcode);

	return ret;
}

int32_t CMSISDAP::finalize(void)
{
	int ret;

	ret = cmdDisconnect();
	if (ret != CMSISDAP_OK)
	{
		return ret;
	}

	ret = cmdLed(_LED_RUNNING, 0);
	if (ret != CMSISDAP_OK) {
		return ret;
	}

	ret = cmdLed(_LED_CONNECT, 0);
	if (ret != CMSISDAP_OK) {
		return ret;
	}

	if (packetBuf != NULL) {
		free(packetBuf);
	}

	if (fwver != NULL) {
		free(fwver);
	}

	if (vendor != NULL) {
		free(vendor);
	}

	if (name != NULL) {
		free(name);
	}

	return ret;
}

int32_t CMSISDAP::resetSw(void)
{
	return cmdSwjPins(_PIN_SWCLK, 1, 0, NULL);
}

int32_t CMSISDAP::resetHw(void)
{
	return cmdSwjPins(_PIN_nRESET, 1, 0, NULL);
}

int32_t CMSISDAP::setSpeed(uint32_t speed)
{
	if (speed > _CMSISDAP_MAX_CLOCK) {
		speed = _CMSISDAP_MAX_CLOCK;
	}

	return cmdSwjClock(speed);
}

void CMSISDAP::setTargetThreadId()
{
	// TODO
}

void CMSISDAP::setCurrentPC(const uint64_t addr)
{
	// TODO
	(void)addr;
}

void CMSISDAP::resume()
{
	// TODO
	// continue command
}

int32_t CMSISDAP::step(uint8_t* signal)
{
	ASSERT_RELEASE(signal != nullptr);

	*signal = 0x05;	// SIGTRAP
	return 0;
}

int32_t CMSISDAP::interrupt(uint8_t* signal)
{
	ASSERT_RELEASE(signal != nullptr);

	*signal = 0x05;	// SIGTRAP
	return 0;
}

int32_t CMSISDAP::setBreakPoint(BreakPointType type, uint64_t addr, uint32_t kind)
{
	return -1;	// not supported
}

int32_t CMSISDAP::unsetBreakPoint(BreakPointType type, uint64_t addr, uint32_t kind)
{
	return -1;	// not supported
}

int32_t CMSISDAP::setWatchPoint(WatchPointType type, uint64_t addr, uint32_t kind)
{
	return -1;	// not supported
}

int32_t CMSISDAP::unsetWatchPoint(WatchPointType type, uint64_t addr, uint32_t kind)
{
	return -1;	// not supported
}

int32_t CMSISDAP::readRegister(const uint32_t n, uint32_t* out)
{
	ASSERT_RELEASE(out != nullptr);
	*out = 0xDEADBEEF;	// dummy
	return 0;
}

int32_t CMSISDAP::readRegister(const uint32_t n, uint64_t* out)
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

int32_t CMSISDAP::readRegister(const uint32_t n, uint64_t* out1, uint64_t* out2)
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

int32_t CMSISDAP::writeRegister(const uint32_t n, const uint32_t data)
{
	// TODO
	(void)n;
	(void)data;
	return 0;
}

int32_t CMSISDAP::writeRegister(const uint32_t n, const uint64_t data)
{
	(void)n;
	(void)data;
	return 0;
}

int32_t CMSISDAP::writeRegister(const uint32_t n, const uint64_t data1, const uint64_t data2)
{
	(void)n;
	(void)data1;
	(void)data2;
	return 0;
}

int32_t CMSISDAP::readGenericRegisters(std::vector<uint32_t>* array)
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

int32_t CMSISDAP::writeGenericRegisters(const std::vector<uint32_t>& array)
{
	// TODO
	(void)array;
	return 0;
}

void CMSISDAP::readMemory(uint64_t addr, uint32_t len, std::vector<uint8_t>* array)
{
	ASSERT_RELEASE(array != nullptr);

	(void)addr;

	for (uint32_t i = 0; i < len; i++)
	{
		array->push_back(0xEF);	// dummy
	}
}

uint8_t CMSISDAP::writeMemory(uint64_t addr, uint32_t len, const std::vector<uint8_t>& array)
{
	// TODO
	(void)addr;
	(void)len;
	(void)array;
	return 0;
}

int32_t CMSISDAP::dpRead(uint32_t reg, uint32_t *data)
{
	int ret;
	uint32_t val;
	uint32_t idx = 0;

	packetBuf[idx++] = _USB_HID_REPORT_NUM;
	packetBuf[idx++] = CMD_TX;
	packetBuf[idx++] = 0x00; /* DAP Index, ignored in the swd. */
	packetBuf[idx++] = 0x01; /* Tx count */
	packetBuf[idx++] = CMSIS_CMD_DP | CMSIS_CMD_READ | CMSIS_CMD_A32(reg);
	ret = usbTx(idx);
	if (ret != CMSISDAP_OK) {
		return ret;
	}
	if ((packetBuf[2] & TX_ACK_FAULT) != 0) {
		ret = CMSISDAP_ERR_ACKFAULT;
		return ret;
	}

	val = buf2LE32(&packetBuf[3]);

	if (data != NULL) {
		*data = val;
	}

	return CMSISDAP_OK;
}

int32_t CMSISDAP::dpWrite(uint32_t reg, uint32_t data)
{
	int ret;
	uint32_t idx = 0;

	packetBuf[idx++] = 0x00; /* report number */
	packetBuf[idx++] = CMD_TX;
	packetBuf[idx++] = 0x00;
	packetBuf[idx++] = 0x01;
	packetBuf[idx++] = CMSIS_CMD_DP | CMSIS_CMD_WRITE | CMSIS_CMD_A32(reg);
	packetBuf[idx++] = (data) & 0xff;
	packetBuf[idx++] = (data >> 8) & 0xff;
	packetBuf[idx++] = (data >> 16) & 0xff;
	packetBuf[idx++] = (data >> 24) & 0xff;
	ret = usbTx(idx);

	if (packetBuf[1] != 0x01) {
		ret = packetBuf[2];
	}
	return CMSISDAP_OK;
}
