/**
 * @file   ymodem.h
 * @author Ed Holmes (edholmes2232(at)gmail.com)
 * @brief  YMODEM software function headers
 */
#ifndef YMODEM_H_
#define YMODEM_H_

/*
 * Includes
 */

#include <stdint.h>
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/*
 * Macros
 */

#ifndef YM_FILE_NAME_LENGTH
#define YM_FILE_NAME_LENGTH			(256)
#endif

#ifndef	YM_FILE_SIZE_LENGTH
#define YM_FILE_SIZE_LENGTH			(16)
#endif

#ifndef YM_RESP_PAYLOAD_LEN
#define YM_RESP_PAYLOAD_LEN			(5)
#endif

/** Regular packet size **/
#define YM_PACKET_SIZE				(128)
/** Data packet size **/
#define YM_PACKET_1K_SIZE			(1024)

#define YM_PACKET_SEQNO_INDEX     	(1)
#define YM_PACKET_SEQNO_COMP_INDEX 	(2)

#define YM_PACKET_HEADER           	(3)
#define YM_PACKET_TRAILER          	(2)
#define YM_PACKET_OVERHEAD         	(YM_PACKET_HEADER + YM_PACKET_TRAILER)

#define YM_PACKET_1K_OVRHD_SIZE		(YM_PACKET_1K_SIZE + YM_PACKET_OVERHEAD)

#define YM_INSTANCE_INIT_MASK		0x52

/*
 * Enumerates
 */

/**
 * @brief  Return values for YMODEM functions
 * 
 */
typedef enum {
	YMODEM_OK = 0,			/* All OK, send next byte */
	YMODEM_TX_PENDING,		/* Data waiting to be transmitted */
	YMODEM_ABORTED,			/* Transfer aborted */
	YMODEM_WRITE_ERR,		/* Error writing to flash */
	YMODEM_SIZE_ERR,		/* File is bigger than flash */
	YMODEM_COMPLETE,		/* Transfer completed succesfully */
} ymodem_err_e;

typedef enum{
	YMODEM_FILE_CB_NAME,
	YMODEM_FILE_CB_DATA,
	YMODEM_FILE_CB_END,
	YMODEM_FILE_CB_ABORTED
} ymodem_file_cb_e;

/*
 * Typedefs
 */

typedef uint8_t (*ymodem_fxn_t)(uint8_t *data, uint32_t len);

/*
 * structs
 */

typedef struct{
	uint8_t 	fileName[YM_FILE_NAME_LENGTH];			/** Incoming file filename **/
	uint8_t 	fileSizeStr[YM_FILE_SIZE_LENGTH];		/** Incoming file size string **/
	uint8_t 	packetData[YM_PACKET_1K_OVRHD_SIZE];	/** Packet Data to hold the received data **/
	uint8_t		payloadTx[YM_RESP_PAYLOAD_LEN];			/** Payload to response the host **/
	uint8_t		payloadLen;								/** Length of the payload to send **/
	uint8_t 	initialized;							/** Initialized flag **/
	uint32_t 	fileSize;								/** File size as int **/
	uint8_t 	prevC;									/** Previous byte character inputted **/
	uint8_t 	startOfPacket; 							/** Whether data is start of a packet **/
	uint8_t 	eotReceived; 							/** Expect one more packet after this to signal end **/
	uint16_t 	packetBytes; 							/** # of Bytes received of current packet **/
	uint16_t 	packetSize;								/** Size of current packet **/
	int32_t 	packetsReceived;						/** Num packets received **/
	ymodem_err_e nextStatus; 	 						/** Status to return after closing a connection **/
	ymodem_fxn_t serialWriteFxn;						/** Function pointer to the routine to write into serial **/
} ymodem_t;


void 			ymodem_Init(ymodem_t *ymodem, ymodem_fxn_t SerialWriteFxn);
ymodem_err_e 	ymodem_ReceiveByte(ymodem_t *ymodem, uint8_t byte);
ymodem_err_e 	ymodem_Reset(ymodem_t *ymodem);

/* Callback */
/**
 * @brief 	Callback to inform a received byte to the main application. Be caution, if the ymodem_ReceiveByte is called
 * 			from an interrupt, this callback is called inside this interrupt. We recommend to do not call the ymodem_ReceiveByte
 * 			from an interrupt context, insted, use a Ring Buffer or Queue to handle these packets.
 * @param	ymodem 		The handler of the YMODEM
 * @param	e			Event to tell what operation type of data was received over the YMODEM
 * @param	data		The data contaning the arrat information. The data is dependent of the 'e' parameter:
 * 						YMODEM_FILE_CB_NAME the data is the fileName received over the protocol.
 * 						YMODEM_FILE_CB_DATA the data contains the raw data of the file.
 * 						YMODEM_FILE_CB_END data is NULL and don't care.
 * 						YMODEM_FILE_CB_ABORT data is NULL.
 * @param 	len			Indicate a lenth of something, but, this length, like the data, is dependent of the 'e'.
 * 						YMODEM_FILE_CB_NAME will indicate the file length.
 * 						YMODEM_FILE_CB_DATA is the amount of data.
 * 						YMODEM_FILE_CB_END don't care.
 * 						YMODEM_FILE_CB_ABORT don't.
 *
 * @ret		Return the operation status. This is very important to generate correct
 */
ymodem_err_e	ymodem_FileCallback(ymodem_t *ymodem, ymodem_file_cb_e e, uint8_t *data, uint32_t len);

#endif // YMODEM_H
