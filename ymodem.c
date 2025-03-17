/**
 * @file   ymodem.c
 * @author Ed Holmes (edholmes2232(at)gmail.com)
 * @brief  Functions for YMODEM protocol
 *         Reference used: http://textfiles.com/programming/ymodem.txt
 *                         https://programmer.ink/think/ymodem-protocol-learning.html
 * 		   To program from unix: use picocom with --send-cmd "sb -vv"
 *         To program from Windows: Use TeraTerm
 *
 * @mod	   This library was modified by Pablo Jean Rozario. Aimming more flexibility
 */
#include "ymodem.h"


/**
 * @brief  YMODEM Control Characters
 * 
 */
enum YM_CC {
	SOH			= 0x01,	 /* start of 128-byte data packet */
	STX			= 0x02,  /* start of 1024-byte data packet */
	EOT			= 0x04,  /* end of transmission */
	ACK			= 0x06,  /* acknowledge */
	NAK			= 0x15,  /* negative acknowledge */
	CA			= 0x18,  /* two of these in succession aborts transfer */
	CRC16		= 0x43,  /* 'C' == 0x43, request 16-bit CRC */
	ABORT1		= 0x41,  /* 'A' == 0x41, abort by user */
	ABORT2		= 0x61,  /* 'a' == 0x61, abort by user */
};


/** Polynomial for CRC calculation **/
#define YM_CRC_POLY		(0x1021)

#define SWAP16(x)		(x >> 8) | ((x & 0xff) << 8)
#define ISVALIDDEC(c) 	((c >= '0') && (c <= '9'))
#define CONVERTDEC(c)	(c - '0')

/**
 * @brief  Internal return values
 * 
 */
typedef enum {
	YM_OK = 0,		/* OK, RETURN NOTHING */
	YM_ABORTED,		/* 2x CA Received, graceful abort, return ACK */
	YM_ABORT,		/* Initiate graceful abort, return 2x CA */
	YM_WRITE_ERR,	/* Error writing to flash */
	YM_SIZE_ERR,	/* File too big */
	YM_START_RX,	/* First frame ok, start receive, return ACK, CRC */
	YM_RX_ERROR,	/* Data receive error, return NAK */
	YM_RX_OK,		/* Data receive ok, return ACK */
	YM_RX_COMPLETE,	/* Data receive complete, return ACK */
	YM_SUCCESS,		/* Transfer complete, close */
} ym_ret_t;




ymodem_err_e 	ymodem_Abort(ymodem_t *ymodem);
static ym_ret_t ymodem_ProcessPacket(ymodem_t *ymodem);
static ym_ret_t ymodem_ProcessFirstPacket(ymodem_t *ymodem);
static ym_ret_t ymodem_ProcessDataPacket(ymodem_t *ymodem);
static ym_ret_t ymodem_CheckCRC(ymodem_t *ymodem);
static void 	ymodem_WriteSerial(ymodem_t *ymodem);

static uint32_t Str2Int(uint8_t *inputstr, uint32_t *intnum);


/**
 * @brief  Initialise YMODEM Rx State 
 * 
 */
void ymodem_Init(ymodem_t *ymodem, ymodem_fxn_t SerialWriteFxn) {
	assert (ymodem != NULL);

	if (ymodem->initialized == YM_INSTANCE_INIT_MASK){
		return;
	}

	memset(ymodem->fileName, 	0, YM_FILE_NAME_LENGTH);
	memset(ymodem->fileSizeStr, 0, YM_FILE_SIZE_LENGTH);
	memset(ymodem->packetData, 	0, YM_PACKET_1K_OVRHD_SIZE);
	ymodem->fileSize 		= 0;
	ymodem->prevC 			= 0;
	ymodem->startOfPacket 	= 1;
	ymodem->packetBytes 	= 0;
	ymodem->packetSize 		= 0;
	ymodem->packetsReceived	= 0;
	ymodem->eotReceived 	= 0;
	ymodem->serialWriteFxn 	= SerialWriteFxn;
	ymodem->nextStatus 		= YMODEM_OK;
	ymodem->initialized 	= YM_INSTANCE_INIT_MASK;
}


/**
 * @brief  				Generates a payload to return to the YMODEM sender. 
 * 
 * @param  retVal		The internal YM_RET_T to translate to YMODEM_T 
 * @param  respBuff		Buffer to write to the response to
 * @param  len			Length of data written
 * @return YMODEM_T 	
 */
static ymodem_err_e GenerateResponse(ymodem_t *ymodem, ym_ret_t retVal) {
	switch (retVal) {
		case YM_OK:
			ymodem->payloadLen = 0;
			return YMODEM_OK;
			break;
		case YM_ABORT:
			ymodem_Abort(ymodem);
			return YMODEM_TX_PENDING;
			break;
		case YM_ABORTED:
			ymodem->payloadTx[0] = CRC16;
			ymodem->payloadLen = 1;
			ymodem->nextStatus = YMODEM_ABORTED;
			return YMODEM_TX_PENDING;
			break;
		case YM_WRITE_ERR:
			ymodem_Abort(ymodem);
			ymodem->nextStatus = YMODEM_WRITE_ERR;
			return YMODEM_TX_PENDING;
		case YM_SIZE_ERR:
			ymodem_Abort(ymodem);
			ymodem->nextStatus = YMODEM_SIZE_ERR;
			return YMODEM_TX_PENDING;
		case YM_START_RX:
			ymodem->payloadTx[0] = ACK;
			ymodem->payloadTx[1] = CRC16;
			ymodem->payloadLen = 2;
			return YMODEM_TX_PENDING;
			break;
		case YM_RX_ERROR:
			ymodem->payloadTx[0] = NAK;
			ymodem->payloadLen = 1;
			return YMODEM_TX_PENDING;
			break;
		case YM_RX_OK:
			ymodem->payloadTx[0] = ACK;
			ymodem->payloadLen = 1;
			return YMODEM_TX_PENDING;
			break;
		case YM_RX_COMPLETE:
			ymodem->payloadTx[0] = ACK;
			ymodem->payloadTx[1] = CRC16;
			ymodem->payloadLen = 2;
			return YMODEM_TX_PENDING;
			break;
		case YM_SUCCESS:
			ymodem->payloadTx[0] = ACK;
			ymodem->payloadLen = 1;
			ymodem->nextStatus = YMODEM_COMPLETE;
			return YMODEM_TX_PENDING;
			break;
		default: 
			break;
	}
	return YMODEM_ABORTED;
}

/**
 * @brief  				Aborts a YMODEM transfer. Writing the aborting commands payload to a provided buffer.
 * 						This buffer needs to be sent to the YMODEM sender.
 * 
 * @param  respBuff		Buffer to write payload to.
 * @param  len			Length of the payload.
 * @return YMODEM_T 		
 */
ymodem_err_e ymodem_Abort(ymodem_t *ymodem) {
	assert (ymodem != NULL);
	assert (ymodem->initialized = YM_INSTANCE_INIT_MASK);

	ymodem->payloadTx[0] = CA;
	ymodem->payloadTx[1] = CA;
	ymodem->payloadLen = 2;
	ymodem->nextStatus = YMODEM_ABORTED;
	ymodem->packetsReceived = 0;
	return YMODEM_ABORTED;
}

/**
 * @brief  				Restart all variables and machine state of ymodem.
 *
 * @param  ymodem		Ymodem instance.
 */
ymodem_err_e 	ymodem_Reset(ymodem_t *ymodem){
	assert (ymodem != NULL);
	assert (ymodem->initialized = YM_INSTANCE_INIT_MASK);

	ymodem->fileSize 		= 0;
	ymodem->prevC 			= 0;
	ymodem->startOfPacket 	= 1;
	ymodem->packetBytes 	= 0;
	ymodem->packetSize 		= 0;
	ymodem->packetsReceived	= 0;
	ymodem->eotReceived 	= 0;
	ymodem->nextStatus 		= YMODEM_OK;

	return YMODEM_OK;
}

/**
 * @brief  				Receives packets from a YMODEM Sender as a byte.
 * 						Bytes should be passed continuously while YMODEM_OK returned.
 * 
 * @param  c			A byte from the YMODEM Sender
 * @param  respBuff		Buffer to write the data to be sent back to the sender
 * @param  respLen		Length of the data to send to the sender.
 * @return YMODEM_T 	Return value indicating status after each byte.
 */
ymodem_err_e ymodem_ReceiveByte(ymodem_t *ymodem, uint8_t c) {
	ym_ret_t ret = YM_OK;
	ymodem_err_e GenRet;

	assert (ymodem != NULL);
	assert (ymodem->initialized = YM_INSTANCE_INIT_MASK);

	/* Return status if just closed connection */
	if (ymodem->nextStatus != YMODEM_OK) return ymodem->nextStatus;

	do {	
		/* Receive full packet */
		if (ymodem->startOfPacket) {
			/* Process start of packet */
			switch (c) {
				case SOH:
					ymodem->packetSize = YM_PACKET_SIZE;
					/* start receiving payload */
					ymodem->startOfPacket = 0;
					ymodem->packetBytes++; //increment by 1 byte
					ret = YM_OK;
					break; 
				case STX:
					ymodem->packetSize = YM_PACKET_1K_SIZE;
					/* start receiving payload */
					ymodem->startOfPacket = 0;
					ymodem->packetBytes++; //increment by 1 byte
					ret = YM_OK;
					break;
				case EOT: 
				/* One more packet comes after with 0,FF so reset this */
					ymodem->eotReceived = 1;
					ret = YM_RX_COMPLETE;
					break;
				case CA:
					/* Two of these aborts transfer */
					if (ymodem->prevC == CA) {
						ret = YM_ABORTED;
						break;
					}
					ret = YM_OK;
					break;
				case ABORT1:
				case ABORT2: 
					ret = YM_ABORT;
					break;
				default: 
					ret = YM_RX_ERROR;
					break;
			}
		} else {
			/* receive rest of packet */
			if (ymodem->packetBytes < (ymodem->packetSize + YM_PACKET_OVERHEAD)-1) {
				ymodem->packetData[ymodem->packetBytes++] = c;
				ret = YM_OK;
				break;
			} else {
				/* Last byte of packet */
				ymodem->packetData[ymodem->packetBytes++] = c;
				if (ymodem->packetData[YM_PACKET_SEQNO_INDEX] != ((ymodem->packetData[YM_PACKET_SEQNO_COMP_INDEX] ^ 0xFF) & 0xFF)) {
					/* Check byte 1 == (byte 2 XOR 0xFF) */
					ymodem->startOfPacket = 1;
					ymodem->packetBytes = 0;
					ret = YM_RX_ERROR;
					break;
				} else {
					/* Full packet received */
					ret = ymodem_ProcessPacket(ymodem);
					ymodem->startOfPacket = 1;
					ymodem->packetBytes = 0;
					break;
				}
			}
		}
	} while (0); // Empty do while to avoid multiple "return" statements
	ymodem->prevC = c;
	GenRet = GenerateResponse(ymodem, ret);
	switch (GenRet){
	case YMODEM_TX_PENDING:
		ymodem_WriteSerial(ymodem);
		break;
	case YMODEM_ABORTED:
		ymodem_FileCallback(ymodem, YMODEM_FILE_CB_ABORTED, NULL, 0);
		break;
	default:

		break;
	}

	return GenRet;
}

static ym_ret_t ymodem_ProcessPacket(ymodem_t *ymodem) {
	ym_ret_t ret = YM_OK;
	do {
		if (ymodem->eotReceived == 1) {
			ymodem_FileCallback(ymodem, YMODEM_FILE_CB_END, NULL, 0);
			ret = YM_SUCCESS;
			break;
		/* Check byte 1 == num of bytes received */
		} else if ((ymodem->packetData[YM_PACKET_SEQNO_INDEX] & 0xFF) != (ymodem->packetsReceived & 0xFF)) {
			/* Send a NAK */
			ret = YM_RX_ERROR;
			break;
		} else if (ymodem_CheckCRC(ymodem) != YM_OK) {
			ret = YM_RX_ERROR;
			break;
		} else {
			if (ymodem->packetsReceived == 0) {
				ret = ymodem_ProcessFirstPacket(ymodem);
				break;
			} else {
				ret = ymodem_ProcessDataPacket(ymodem);
				break;
			}
		}
	} while(0);
	return ret;
}

/**
 * @brief  				Writes a data packet to the flash set in ymodem_conf.h
 * 						Optionally validates the writes.
 * 
 * @return YM_RET_T 	YM_WRITE_ERR if write fails, otherwise YM_RX_OK
 */
static ym_ret_t ymodem_ProcessDataPacket(ymodem_t *ymodem) {
	ym_ret_t ret;
	ymodem_err_e err;
	uint8_t *buffIn;

	do { 
		buffIn = (uint8_t *)ymodem->packetData + YM_PACKET_HEADER;
		err = ymodem_FileCallback(ymodem, YMODEM_FILE_CB_DATA, buffIn, ymodem->packetSize);
		if (err == YMODEM_OK){
			ret = YM_RX_OK;
		}
		else{
			ret = YM_WRITE_ERR;
		}
		ymodem->packetsReceived++;

	} while(0);
	return ret;
}


/**
 * @brief  			Gets data from the first YMODEM packet.
 * 
 * @return YM_RET_T 
 */
static ym_ret_t ymodem_ProcessFirstPacket(ymodem_t *ymodem) {
	ym_ret_t ret;
	ymodem_err_e err;
	int32_t i; 
	uint8_t *filePtr; 
	do {
		/* Filename packet */
		if (ymodem->packetData[YM_PACKET_HEADER] != 0) {
			/* Packet has valid data */
			/* Get File Name */
			filePtr = ymodem->packetData + YM_PACKET_HEADER;
			i = 0;
			while ((i < YM_FILE_NAME_LENGTH) && (*filePtr != '\0')){
				ymodem->fileName[i++] = *filePtr++;
			}
			ymodem->fileName[i++] = '\0';

			i = 0;
			filePtr++;
			while ((i < YM_FILE_SIZE_LENGTH)  && (*filePtr != ' ')){
				ymodem->fileSizeStr[i++] = *filePtr++;
			}
			ymodem->fileSizeStr[i++] = '\0';
			Str2Int(ymodem->fileSizeStr, &ymodem->fileSize);

			err = ymodem_FileCallback(ymodem, YMODEM_FILE_CB_NAME, ymodem->fileName, ymodem->fileSize);
			if (err == YMODEM_OK){
				ret = YM_START_RX;
			}
			else{
				ret = YM_SIZE_ERR;
			}
			ymodem->packetsReceived++;
			break;

		} else {
			/* Filename packet is empty, end session */ 
			ret = YM_ABORT;
			break;
		}

	} while (0);
	return ret;
}

static void ymodem_WriteSerial(ymodem_t *ymodem){
	if (ymodem->serialWriteFxn != NULL){
		ymodem->serialWriteFxn(ymodem->payloadTx, ymodem->payloadLen);
	}
}

static uint32_t Str2Int(uint8_t *inputstr, uint32_t *intnum) {
	uint32_t i = 0, res = 0;
	uint32_t val = 0;

	/* max 10-digit decimal input */
	for (i = 0; i < 11; i++) {
		if (inputstr[i] == '\0') {
			*intnum = val;
			/* return 1 */
			res = 1;
			break;
		} else if (ISVALIDDEC(inputstr[i])) {
			val = val * 10 + CONVERTDEC(inputstr[i]);
		} else {
			/* return 0, Invalid input */
			res = 0;
			break;
		}
	}
	/* Over 10 digit decimal --invalid */
	if (i >= 11) {
		res = 0;
	}
	return res;
}

static uint16_t crc_update(uint16_t crc_in, int incr)
{
        uint16_t xor = crc_in >> 15;
        uint16_t out = crc_in << 1;

        if (incr)
                out++;

        if (xor)
                out ^= YM_CRC_POLY;

        return out;
}


static uint16_t crc16(const uint8_t *data, uint16_t size)
{
        uint16_t crc, i;

        for (crc = 0; size > 0; size--, data++)
                for (i = 0x80; i; i >>= 1)
                        crc = crc_update(crc, *data & i);

        for (i = 0; i < 16; i++)
                crc = crc_update(crc, 0);

        return crc;
}

static ym_ret_t ymodem_CheckCRC(ymodem_t *ymodem) {
	uint16_t sourceCRC = 0;
	sourceCRC = ymodem->packetData[(ymodem->packetSize+YM_PACKET_OVERHEAD) - 1];
	sourceCRC = (sourceCRC << 8) | ymodem->packetData[(ymodem->packetSize+YM_PACKET_OVERHEAD) - 2];

	uint16_t newCRC = SWAP16(crc16(ymodem->packetData+YM_PACKET_HEADER, ymodem->packetSize));
	if (newCRC != sourceCRC) {
		return YM_RX_ERROR;
	} else {
		return YM_OK;
	}
}
