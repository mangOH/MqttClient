/*******************************************************************************
 * MQTT class for Sierra Wireless' HL Serie Modules
 * wrapping Paho's MQTTClient and using TCP_HL layer (replacing the default ipstack)
 *
 * Nhon Chu - May 2015 - V0.92
 *******************************************************************************/

#include "swir_mqtt.h"


#include "MQTTPublish.h"
#include "MQTTPacket.h"

#include "swir_json.h"
#include "swir_debug.h"

#include <stdlib.h>

#define 	TOPIC_NAME_PUBLISH			"/messages/json"
#define 	TOPIC_NAME_SUBSCRIBE		"/tasks/json"
#define		TOPIC_NAME_ACKCMD			"/acks/json"
#define 	URL_AIRVANTAGE_SERVER		"eu.airvantage.net"
#define 	PORT_AIRVANTAGE_SERVER		1883

#define		MAX_SIZE_128				128
#define		MAX_SIZE_256				256

#define		AV_JSON_KEY_MAX_COUNT		10		//max number of Key in AV-MQTT JSON payload
#define		AV_JSON_KEY_MAX_LENGTH		32		//default max length of Key Name


SWIR_MQTTClient::inMessageHandler SWIR_MQTTClient::_pfnInMsgCallback = NULL;
SWIR_MQTTClient* SWIR_MQTTClient::_pThisClient = NULL;

SWIR_MQTTClient::SWIR_MQTTClient(MODULE_SERIAL_CLASS &module)
{
	_pMqttClient = new MQTT::Client<SWIR_TCP_HL, COUNTDOWN_CLASS, MAX_PAYLOAD_SIZE, 1>(_swirModule);
	SWIR_MQTTClient::_pfnInMsgCallback = NULL;

	_ePublishQoS = MQTT::QOS0;

	_pszSubscribedTopic = NULL;
	_pszPublishTopic = NULL;
	_pszAckCmdTopic = NULL;

	_swirModule.setSerialObject(module);
}

void SWIR_MQTTClient::setBaudRate(
		long 			nBaudrate)
{
	_swirModule.setBaudRate(nBaudrate);
}

int SWIR_MQTTClient::setSimPIN(int nPinCode)
{
	#ifdef _VERBOSE_DEBUG_
	SWIR_TRACE(F("MQTTClient::setSimPIN"));
	#endif
	return _swirModule.setSimPIN(nPinCode);
}

int SWIR_MQTTClient::isSimReady()
{
	return _swirModule.isSimReady();
}

int SWIR_MQTTClient::setAPN(char* szAPN, char* szAPNlogin, char * szAPNpassword)
{
	if ( (szAPNlogin == NULL) && (szAPNpassword == NULL) )
	{
		return _swirModule.setAPN(szAPN, "", "");
	}
	else if (szAPNlogin == NULL)
	{
		return _swirModule.setAPN(szAPN, "", szAPNpassword);
	}
	else if (szAPNpassword == NULL)
	{
		return _swirModule.setAPN(szAPN, szAPNlogin, "");
	}
	else
	{
		return _swirModule.setAPN(szAPN, szAPNlogin, szAPNpassword);
	}
}

int SWIR_MQTTClient::connect(char* szIdentifier, char* szPassword, unsigned long ulKeepAlive)
{
	if (0 == _swirModule.connect(URL_AIRVANTAGE_SERVER, PORT_AIRVANTAGE_SERVER))
	{
		#ifdef _SWIR_OUTPUT_
		SWIR_TRACE(F("MQTT connecting"));
		#endif
		MQTTPacket_connectData data = MQTTPacket_connectData_initializer;       
		data.MQTTVersion = 3;
		data.willFlag = 0;
		data.clientID.cstring = szIdentifier;
		data.username.cstring = szIdentifier;
		data.password.cstring = szPassword;
		data.keepAliveInterval = ulKeepAlive;
		data.cleansession = 1;

		int rc = _pMqttClient->connect(data);

		SWIR_AVAILABLE_SRAM

		if (_pszSubscribedTopic)
		{
			free(_pszSubscribedTopic);
		}
		_pszSubscribedTopic = (char *) malloc(strlen(szIdentifier) + strlen(TOPIC_NAME_SUBSCRIBE) + 1);
		sprintf(_pszSubscribedTopic, "%s%s", szIdentifier, TOPIC_NAME_SUBSCRIBE);

		if (_pszPublishTopic)
		{
			free(_pszPublishTopic);
		}
		_pszPublishTopic = (char *) malloc(strlen(szIdentifier) + strlen(TOPIC_NAME_PUBLISH) + 1);
		sprintf(_pszPublishTopic, "%s%s", szIdentifier, TOPIC_NAME_PUBLISH);
		
		if (_pszAckCmdTopic)
		{
			free(_pszAckCmdTopic);
		}
		_pszAckCmdTopic = (char *) malloc(strlen(szIdentifier) + strlen(TOPIC_NAME_ACKCMD) + 1);
		sprintf(_pszAckCmdTopic, "%s%s", szIdentifier, TOPIC_NAME_ACKCMD);
		
		SWIR_AVAILABLE_SRAM

#ifdef _SWIR_OUTPUT_
		if (rc != 0)
		{
			SWIR_TRACE(F("\r\n---- FAIL MQTT connect (%d) ----"), rc);
		}
		else
		{
			SWIR_TRACE(F("\r\n---- MQTT connected ----\r\n"));
		}
#endif
		return rc;
	}
	else
	{
#ifdef _SWIR_OUTPUT_
		SWIR_TRACE(F("\r\n---- FAIL MQTT connect ----\r\n"));
#endif
		return 1;
	}
}

int SWIR_MQTTClient::connect(char* szPassword, unsigned long ulKeepAlive)
{
	if (!_swirModule.canConnect())
	{
		return 3;
	}

	char  szIMEI[16];
	char  szSN[32];
	char  szModuleType[16];

	_swirModule.getSN(szSN, sizeof(szSN));
	_swirModule.getModuleType(szModuleType, sizeof(szModuleType));

	if (_swirModule.getIMEI(szIMEI, sizeof(szIMEI)) != 0)
	{
		SWIR_TRACE(F("Failed to retrieve IMEI: %s"), szIMEI);
		return 2;
	}
	
	if (0 == _swirModule.connect(URL_AIRVANTAGE_SERVER, PORT_AIRVANTAGE_SERVER))
	{
		#ifdef _SWIR_OUTPUT_
		SWIR_TRACE(F("MQTT connecting"));
		#endif
		SWIR_AVAILABLE_SRAM
		MQTTPacket_connectData data = MQTTPacket_connectData_initializer;       
		data.MQTTVersion = 3;
		data.willFlag = 0;
		data.clientID.cstring = szIMEI;
		data.username.cstring = szIMEI;
		data.password.cstring = szPassword;
		data.keepAliveInterval = ulKeepAlive;
		data.cleansession = 1;

		int rc = _pMqttClient->connect(data);

		if (_pszSubscribedTopic)
		{
			free(_pszSubscribedTopic);
		}
		_pszSubscribedTopic = (char *) malloc(strlen(szIMEI) + strlen(TOPIC_NAME_SUBSCRIBE) + 1);
		sprintf(_pszSubscribedTopic, "%s%s", szIMEI, TOPIC_NAME_SUBSCRIBE);

		if (_pszPublishTopic)
		{
			free(_pszPublishTopic);
		}
		_pszPublishTopic = (char *) malloc(strlen(szIMEI) + strlen(TOPIC_NAME_PUBLISH) + 1);
		sprintf(_pszPublishTopic, "%s%s", szIMEI, TOPIC_NAME_PUBLISH);

		if (_pszAckCmdTopic)
		{
			free(_pszAckCmdTopic);
		}
		_pszAckCmdTopic = (char *) malloc(strlen(szIMEI) + strlen(TOPIC_NAME_ACKCMD) + 1);
		sprintf(_pszAckCmdTopic, "%s%s", szIMEI, TOPIC_NAME_ACKCMD);
		
		SWIR_AVAILABLE_SRAM

#ifdef _SWIR_OUTPUT_
		if (rc != 0)
		{
			SWIR_TRACE(F("\r\n---- FAIL MQTT connect (%d) ----"), rc);
			SWIR_TRACE(F("Please register your module on AirVantage portal or Check the Password:"));
			SWIR_TRACE(F("Module Type: %s"), szModuleType);
			SWIR_TRACE(F("Serial Number: %s"), szSN);
			SWIR_TRACE(F("IMEI: %s\r\n"), szIMEI);
		}
		else
		{
			SWIR_TRACE(F("\r\n---- MQTT connected ----\r\n"));
		}
#endif
		return rc;
	}
	else
	{
#ifdef _SWIR_OUTPUT_
		SWIR_TRACE(F("\r\n---- FAIL MQTT connect ----\r\n"));
#endif
		return 1;
	}
}


int SWIR_MQTTClient::isConnected()
{
	//return _swirModule.isConnected();
	SWIR_AVAILABLE_SRAM
	return _pMqttClient->isConnected();
}

int SWIR_MQTTClient::isDataCallReady()
{
	return _swirModule.canConnect();
}

void SWIR_MQTTClient::disconnect()
{
	_swirModule.disconnect();
	if (_pMqttClient != NULL)
	{
		_pMqttClient->disconnect();
	}
	if (_pszSubscribedTopic)
	{
		free(_pszSubscribedTopic);
		_pszSubscribedTopic = NULL;
	}
	if (_pszPublishTopic)
	{
		free(_pszPublishTopic);
		_pszPublishTopic = NULL;
	}
	if (_pszAckCmdTopic)
	{
		free(_pszAckCmdTopic);
		_pszAckCmdTopic = NULL;
	}
}

void SWIR_MQTTClient::loop()
{
	if (_pMqttClient != NULL)
	{
		_pMqttClient->yield(10000);
	}
}

void	SWIR_MQTTClient::setPublishQos(int nQoS)
{
	SWIR_AVAILABLE_SRAM

	switch (nQoS)
	{
		case 1:
			_ePublishQoS = MQTT::QOS1;
			break;
		case 2:
			_ePublishQoS = MQTT::QOS2;
			break;
		default:
			_ePublishQoS = MQTT::QOS0;
			break;
	}
}

int SWIR_MQTTClient::publishAckCmd(const char* szUid, int nAck, char* szMessage)
{
	int rc = 1;

#ifdef _SWIR_OUTPUT_
	SWIR_TRACE(F("Publishing: Ack%d to %s"), nAck, _pszAckCmdTopic);
#endif

	if (strlen(_pszAckCmdTopic) > 0)
	{
		char* szPayload = (char*) malloc(strlen(szUid)+strlen(szMessage)+48);

		if (nAck == 0)
		{
			sprintf(szPayload, "[{\"uid\": \"%s\", \"status\" : \"OK\"", szUid);
		}
		else
		{
			sprintf(szPayload, "[{\"uid\": \"%s\", \"status\" : \"ERROR\"", szUid);
		}

		if (strlen(szMessage) > 0)
		{
			sprintf(szPayload, "%s, \"message\" : \"%s\"}]", szPayload, szMessage);
		}
		else
		{
			sprintf(szPayload, "%s}]", szPayload);
		}
		

#ifdef _SWIR_OUTPUT_
	SWIR_TRACE(F("  Ack payload: %s"), szPayload);
#endif
		MQTT::Message		msg;

		msg.qos = _ePublishQoS;
		msg.retained = 0;
		msg.dup = 0;
		msg.id = 0;
		msg.payload = szPayload;
		msg.payloadlen = strlen(szPayload);

#ifdef _VERBOSE_DEBUG_
		SWIR_TRACE(F("Publishing Payload:"));
#endif

		rc = _pMqttClient->publish(_pszAckCmdTopic, msg);
		SWIR_AVAILABLE_SRAM

#ifdef _SWIR_OUTPUT_
		if (rc != 0)
		{
			SWIR_TRACE(F("FAILED MQTT publish"));
		}
		else
		{
			SWIR_TRACE(F("MQTT publish OK"));
		}
		
		if (szPayload)
		{
			free(szPayload);
		}
#endif
	}

	return rc;
}


int SWIR_MQTTClient::publish(char* szKey, char* szValue, unsigned long ulTimestamp)
{
	int rc = 1;

#ifdef _SWIR_OUTPUT_
	SWIR_TRACE(F("Publishing: %s=%s to %s"), szKey, szValue, _pszPublishTopic);
#endif

	if (strlen(_pszPublishTopic) > 0)
	{
		SwirJson	oJson;

		char* szPayload = oJson.serialize(szKey, szValue);

		MQTT::Message		msg;

		msg.qos = _ePublishQoS;
		msg.retained = 0;
		msg.dup = 0;
		msg.id = 0;
		msg.payload = szPayload;
		msg.payloadlen = strlen(szPayload);

#ifdef _VERBOSE_DEBUG_
		SWIR_TRACE(F("Publishing Payload:"));
#endif
#ifdef _SWIR_OUTPUT_
	SWIR_TRACE(F("  Payload: %s"), szPayload);
#endif
		rc = _pMqttClient->publish(_pszPublishTopic, msg);
		SWIR_AVAILABLE_SRAM
		free(szPayload);
		SWIR_AVAILABLE_SRAM

#ifdef _SWIR_OUTPUT_
		if (rc != 0)
		{
			SWIR_TRACE(F("FAILED MQTT publish"));
		}
		else
		{
			SWIR_TRACE(F("MQTT publish OK"));
		}
#endif
	}

	return rc;
}

int SWIR_MQTTClient::publish(char* szKey, double dValue, unsigned long ulTimestamp)
{
	char szValue[16];

#ifdef TARGET_ARDUINO
	dtostrf(dValue, 8, 2, szValue);
#endif
#ifdef TARGET_MBED
	sprintf(szValue, "%.2f", dValue);
#endif
	return publish(szKey, szValue, ulTimestamp);
}

int SWIR_MQTTClient::publish(char* szKey, int nValue, unsigned long ulTimestamp)
{
	char szValue[8];

	sprintf(szValue, "%d", nValue);

	return publish(szKey, szValue, ulTimestamp);
}

int SWIR_MQTTClient::publish(char* szKey, unsigned long ulValue, unsigned long ulTimestamp)
{
	char szValue[16];

	sprintf(szValue, "%lu", ulValue);

	return publish(szKey, szValue, ulTimestamp);
}

void SWIR_MQTTClient::incomingMessageHandler(MQTT::MessageData& inMessage)
{
#ifdef _SWIR_OUTPUT_
	SWIR_TRACE(F("SWIR_MQTTClient::incomingMessageHandler"));
#endif
	if (_pfnInMsgCallback == NULL)
	{
		return;
	}

	MQTT::Message &message = inMessage.message;
	
	char* szPayload = (char *) malloc(message.payloadlen+1);

	memcpy(szPayload, (char*)message.payload, message.payloadlen);
	szPayload[message.payloadlen] = 0;

#ifdef _SWIR_OUTPUT_
	SWIR_TRACE(F("Received MQTT msg:\r\n   %s"), szPayload);
#endif

	SWIR_AVAILABLE_SRAM

	//decode JSON payload
	SwirJson 	oJson;
	
	int 		nAck = 0;

	SWIR_AVAILABLE_SRAM

	char* pszCommand = oJson.getValue(szPayload, -1, "command");
	if (pszCommand)
	{
		SWIR_AVAILABLE_SRAM
		char*	pszUid = oJson.getValue(szPayload, -1, "uid");
		char*	pszTimestamp = oJson.getValue(szPayload, -1, "timestamp");
		char*	pszId = oJson.getValue(pszCommand, -1, "id");
		char*	pszParam = oJson.getValue(pszCommand, -1, "params");

		char	szErrorMessage[MAX_SIZE_128];
		
		szErrorMessage[0] = 0;	//empty string
		
		SWIR_AVAILABLE_SRAM

		for (int i=0; i<AV_JSON_KEY_MAX_COUNT; i++)
		{
			char szKey[AV_JSON_KEY_MAX_LENGTH];

			char * pszValue = oJson.getValue(pszParam, i, szKey);

			SWIR_AVAILABLE_SRAM

			if (pszValue)
			{
				int nRet = handleCallback(pszId, szKey, pszValue, pszTimestamp);	

				if (nRet != 0)
				{
					sprintf(szErrorMessage, "%s - %s unknown", szErrorMessage, szKey);
					nAck = nRet;
				}

				free(pszValue);
			}
			else
			{
				break;
			}
		}

		if (_pThisClient)
		{
			//_pThisClient->publishAckCmd(pszUid, nAck);
			_pThisClient->publishAckCmd(pszUid, 0, szErrorMessage);		//for now, must always returns OK
		}
		
		free(pszCommand);
		if (pszId)
		{
			free(pszId);
		}
		if (pszParam)
		{
			free(pszParam);
		}
		if (pszTimestamp)
		{
			free(pszTimestamp);
		}
		if (pszUid)
		{
			free(pszUid);
		}
	}
#ifdef _SWIR_OUTPUT_
	SWIR_TRACE(F("MQTT msg decoded"));
#endif
	if (szPayload)
	{
		free(szPayload);
	}
	SWIR_AVAILABLE_SRAM
}

int SWIR_MQTTClient::handleCallback(char* szPath, char* szKey, char* szValue, char* szTimestamp)
{
	/*
	This function converts the provided JSON-decoded data (from MQTT payload) to CSV data (key;value;timestamp)
	*/

	if (_pfnInMsgCallback == NULL)
	{
		return 1;
	}

	if (!szPath || !szKey || !szValue || !szTimestamp)
	{
		return 1;
	}
	
	char	szKeyName[MAX_SIZE_128];

	sprintf(szKeyName, "%s.%s", szPath, szKey);
	return SWIR_MQTTClient::_pfnInMsgCallback(szKeyName, szValue, szTimestamp);
}

int SWIR_MQTTClient::subscribe(inMessageHandler pfnCallback)
{
	int rc = 1;

	if (strlen(_pszSubscribedTopic) > 0)
	{
		_pfnInMsgCallback = pfnCallback;
		_pThisClient = this;
		rc = _pMqttClient->subscribe(_pszSubscribedTopic, MQTT::QOS0, incomingMessageHandler);
#ifdef _SWIR_OUTPUT_
		if (rc != 0)
		{
			SWIR_TRACE(F("\r\n---- MQTT Subscribe FAILED ----\r\n"));
		}
		else
		{
			SWIR_TRACE(F("\r\n---- MQTT Subscribe OK ----\r\n"));
		}
#endif
	}

	return rc;
}

void	SWIR_MQTTClient::setDebugPort(
					PORT_TYPE 		nRXport,
					PORT_TYPE 		nTXport,
					long 			nBaudrate)
{
	#ifdef _SWIR_OUTPUT_
	_tracer.setPort(nRXport, nTXport, nBaudrate);
	#endif
}

int SWIR_MQTTClient::getSignalQuality(int &nRssi, int &nBer, int &nEcLo)
{
	return _swirModule.getSignalQuality(nRssi, nBer, nEcLo);
}
