#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include "amcom.h"

/// Start of packet character
const uint8_t  AMCOM_SOP         = 0xA1;
const uint16_t AMCOM_INITIAL_CRC = 0xFFFF;

static uint16_t AMCOM_UpdateCRC(uint8_t byte, uint16_t crc)
{
	byte ^= (uint8_t)(crc & 0x00ff);
	byte ^= (uint8_t)(byte << 4);
	return ((((uint16_t)byte << 8) | (uint8_t)(crc >> 8)) ^ (uint8_t)(byte >> 4) ^ ((uint16_t)byte << 3));
}


void AMCOM_InitReceiver(AMCOM_Receiver* receiver, AMCOM_PacketHandler packetHandlerCallback, void* userContext) {
		receiver->receivedPacketState = AMCOM_PACKET_STATE_EMPTY;
    receiver->packetHandler = packetHandlerCallback;
    receiver->userContext = userContext;
}

size_t AMCOM_Serialize(uint8_t packetType, const void* payload, size_t payloadSize, uint8_t* destinationBuffer) {
	uint16_t crc = AMCOM_INITIAL_CRC;
	size_t packetSize = 0;
	destinationBuffer[packetSize++] = AMCOM_SOP;
	destinationBuffer[packetSize++] = packetType;
	crc = AMCOM_UpdateCRC(packetType, crc);
	destinationBuffer[packetSize++] = (uint8_t)(payloadSize);
	crc = AMCOM_UpdateCRC((uint8_t)(payloadSize), crc);
	size_t crcIndex = packetSize;
	packetSize += 2; // reserve space for CRC
	memcpy(destinationBuffer + packetSize, payload, payloadSize);
	packetSize += payloadSize;
	for (size_t i = 0; i < payloadSize; i++) {
		crc = AMCOM_UpdateCRC(((uint8_t*)payload)[i], crc);
	}
	destinationBuffer[crcIndex] = (uint8_t)(crc & 0x00ff);
	destinationBuffer[crcIndex + 1] = (uint8_t)(crc >> 8);

	return packetSize;
}

void AMCOM_Deserialize(AMCOM_Receiver* receiver, const void* data, size_t dataSize) {

	const uint8_t* bytes = (const uint8_t*) data;
	for (size_t i = 0; i < dataSize; i++) {
		uint8_t byte = bytes[i];

		switch (receiver->receivedPacketState) {
			case AMCOM_PACKET_STATE_EMPTY:
				if(byte == AMCOM_SOP) {
					receiver->receivedPacketState = AMCOM_PACKET_STATE_GOT_SOP;
				}
				break;
			case AMCOM_PACKET_STATE_GOT_SOP:
				receiver->receivedPacket.header.type = byte;
				receiver->receivedPacketState = AMCOM_PACKET_STATE_GOT_TYPE;
				break;
			case AMCOM_PACKET_STATE_GOT_TYPE:
				receiver->receivedPacket.header.length = byte;
				receiver->receivedPacketState = AMCOM_PACKET_STATE_GOT_LENGTH;
				break;
			case AMCOM_PACKET_STATE_GOT_LENGTH:
				receiver->receivedPacket.header.crc = byte;
				receiver->receivedPacketState = AMCOM_PACKET_STATE_GOT_CRC_LO;
				break;
			case AMCOM_PACKET_STATE_GOT_CRC_LO:
				receiver->receivedPacket.header.crc |= ((uint16_t)byte << 8);
				receiver->receivedPacketState = AMCOM_PACKET_STATE_GETTING_PAYLOAD;
				receiver->payloadCounter = 0;
				if(receiver->receivedPacket.header.length == 0) {
				    receiver->receivedPacketState = AMCOM_PACKET_STATE_GOT_WHOLE_PACKET;
				}
				break;
			case AMCOM_PACKET_STATE_GETTING_PAYLOAD:
			    if(receiver->payloadCounter < receiver->receivedPacket.header.length) {
       				receiver->receivedPacket.payload[receiver->payloadCounter++] = byte;
   					
   					if (receiver->payloadCounter == receiver->receivedPacket.header.length) {
					    receiver->receivedPacketState = AMCOM_PACKET_STATE_GOT_WHOLE_PACKET;
					    receiver->payloadCounter = 0;
				    }
			    }
				break;
			default:
				break;					
		}
	}
	
	if(receiver->receivedPacketState == AMCOM_PACKET_STATE_GOT_WHOLE_PACKET) {
	    uint16_t crc = AMCOM_INITIAL_CRC;
		crc = AMCOM_UpdateCRC(receiver->receivedPacket.header.type, crc);
		crc = AMCOM_UpdateCRC(receiver->receivedPacket.header.length, crc);
		for(size_t i = 0; i < receiver->receivedPacket.header.length; i++) {
	        crc = AMCOM_UpdateCRC(receiver->receivedPacket.payload[i], crc);
	    }
		if(crc != receiver->receivedPacket.header.crc) {
			receiver->receivedPacketState = AMCOM_PACKET_STATE_EMPTY;
			return;
		}
		receiver->packetHandler(&receiver->receivedPacket, receiver->userContext);
		receiver->receivedPacketState = AMCOM_PACKET_STATE_EMPTY;
	}
}
