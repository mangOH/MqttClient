/*******************************************************************************************************************
 
 MQTT Spooler v1.0

	This tool automatically converts CVS data to MQTT messages prior publishing to Sierra Wireless Airvantage server.
	It also automatically converts incoming MQTT messages to CSV data.

	This tool could be reused as a quick integration option with your existing sensor/actuator based application.


	Launching MQTT Spooler :
		Mandatory ordered arguments:
			1. IMEI
			2. password (as registered on Airvantage, during Application Model assignment)
			3. Spooling directory name (outbound), must already exists : CVS to MQTT (for publishing)
			4. Inbound data directory name, must already exists : Subcribed-topic MQTT incoming message to CSV
		Optional argument (5th):
			5. Spooling frequency in number of seconds

	Usage :
		- Drop CSV (Key;Value;timestamp) text file into the outbound folder (3rd argument)
		    - The spooler detects this new file, convert CSV content to MQTT payload and publish it to Airvantage
		    - Sensor data can be collected by your Sensor-Agent component then converted to CSV prior saving to
			outbound folder
		- MQTT incoming message are automatically convert to CSV data and persisted in the Inboubd folder (4th argument)
			- Your Actuator-Agent component can parse this inbound folder for incoming trigger-action to be sent
			to actuators

	Key Benefits:
		- Easy integration: the interface between your Agents and MQTT Spooler is simply the CSV file format
					(loosely coupling)
		- Can send and receiving MQTT message without writing code
		- No need to deal with MQTT code

	Author: Nhon Chu (nchu@sierrawireless.com)
   			March 2015

*******************************************************************************************************************/

#include <stdio.h>
#include "MQTTClient.h"
#include "json/swir_json.h"

#include <stdio.h>
#include <signal.h>
#include <memory.h>

#include <sys/time.h>
#include <pthread.h>
#include <dirent.h>

/*--------- Imported functions -----------------------*/
void 		mqtt_publish(char*, int);

/*---------- Defines ---------------------------------*/
#define 	MAX_DATA_COUNT				50	//Default Max messages to be spooled at once, increase it if needed
#define 	MAX_PATH					256	//default max length of full path filename
#define 	MAX_PAYLOAD_SIZE			2048	//Default payload buffer size
#define 	MAX_VALUE_LENGTH			64		//Default max length of value
#define 	TIMEOUT_MS					1000	//1 second time-out, MQTT client init
#define 	MQTT_VERSION				3
#define 	SPOOLING_FREQUENCY_SECOND	30		//Spool for CSV every 30 seconds by default
#define 	TOPIC_NAME_PUBLISH			"/messages/json"
#define 	TOPIC_NAME_SUBSCRIBE		"/tasks/json"
#define 	TOPIC_NAME_ACK				"/acks/json"
#define 	URL_AIRVANTAGE_SERVER		"eu.airvantage.net"
#define 	PORT_AIRVANTAGE_SERVER		1883

#define         AV_JSON_KEY_MAX_COUNT           10
#define         AV_JSON_KEY_MAX_LENGTH          32
//#define 	OFFLINE						//no server connection, for debugging purposes

/*---------- Structures definition -------------------*/
typedef struct st_dataObject DataObject;
struct st_dataObject
{
	char			szName[MAX_VALUE_LENGTH];
	char			szValue[MAX_VALUE_LENGTH];
	int 			bProcessed;
	unsigned long	ulTimeStamp;
};

typedef struct
{
	Client*			pMqttClient;
	char			szOutboundDataFolder[MAX_PATH];
} ST_THREAD1_PARAM;


/*----------- Global variables -----------------------*/
long		g_SpoolingFrequency = SPOOLING_FREQUENCY_SECOND;		//default spooling frequency

char		g_szDeviceId[] = "00000000B6AF4AFF";	//should hold IMEI
int 		g_toStop = 0;						//set to 1 in onExit() to exit program

DataObject	g_astDataObject[MAX_DATA_COUNT];	//holding generic outbound variables to be spooled
int			g_nDataObjectCount;					//number of genenric variables to be spooled

char		g_szInboundDataFolder[MAX_PATH];	//incoming MQTT messages are converted to CSV then stored in this folder

ST_THREAD1_PARAM	g_stSpoolingThreadParam;

//----------------------------------- Functions ---------------------------------------------------------
void onExit(int sig)
{
	signal(SIGINT, NULL);
	g_toStop = 1;
}

//-------------------------------------------------------------------------------------------------------
int getTimeStamp()
{
    time_t stime = time(NULL);
    return stime;
}

//-------------------------------------------------------------------------------------------------------
int parseData(char* filename)
{
	/*
		Parse the given filename to extract all CSV lines to be converted to MQTT messages
		Each CSV line shall have the following formatting:
			DataKeyPath;Value;Timestamp
				DataKeyPath : should be in accordance to the Application Model's data model definition
								e.g. machine.temperature, machine.humidity
				Value : value of the DataKeyPath
				Timestamp : elapsed time in seconds since January 1st 1970.
							Current timestamp will be provided by default if there is no value specified
	*/

	FILE*	file = NULL;
	int 	nCount = 0;
	char	szLine[MAX_PAYLOAD_SIZE];

	printf("Parsing file: %s...\n", filename);
	fflush(stdout);
	file = fopen(filename,"r");

	if (file == NULL)
	{
		printf("Failed to open %s\n", filename);
		perror("Error: ");
		fflush(stdout);
		return nCount;
	}

	while (fgets(szLine, sizeof(szLine), file) != NULL)
	{
		char*			pVal1;
		char*			pVal2;
		char			szKey[MAX_PATH];
		unsigned long	ulValue2;

		printf("Parsing: %s", szLine);	//searching for pattern key;value;value2  (value2 could be the timestamp)
		fflush(stdout);

		if (g_nDataObjectCount < MAX_DATA_COUNT)
		{
			//still have room for new entry

			//find the first separator
			pVal1 = strstr(szLine, ";");
			if (pVal1 != NULL)
			{
				*pVal1 = 0;						//zero terminate the key
				pVal2 = strstr(++pVal1, ";");	//find the second separator
				if (pVal2 != NULL)
				{
					//OK, found the pattern, let's the validity
					*pVal2 = 0; //zero terminate the value
					strcpy(szKey, szLine);
					char* pstr;
					ulValue2 = strtoul(++pVal2, &pstr, 10);		//timestamp

					if (ulValue2 == 0)
					{
						//no user provided timestamp, use current time by default
						printf("null timestamp... providing default one\n");
						ulValue2 = (unsigned long) getTimeStamp();
					}

					strcpy(g_astDataObject[g_nDataObjectCount].szName, szKey);
					strcpy(g_astDataObject[g_nDataObjectCount].szValue, pVal1);
					g_astDataObject[g_nDataObjectCount].ulTimeStamp = ulValue2;
					g_astDataObject[g_nDataObjectCount].bProcessed = 0;	//mark as not processed
					g_nDataObjectCount++;
					printf(">>Read: %s=%s @ %lu\n", szKey, pVal1, ulValue2);
					fflush(stdout);
					continue;
				}
			}
		}
		printf("Line ignored\n");
		fflush(stdout);
	}

	printf("Completed\n");
	fflush(stdout);
	fclose(file);

	//For now just delete the file.... In future improvement, only delete entries that have actually been sent to AirVantage
	remove(filename);

	return nCount;
}

//-------------------------------------------------------------------------------------------------------
int scanOutboundFolder(char* scanOutboundFolderName)
{
	/*
		Scan the given scanOutboundFolderName :
		For each file found in this directory, call parseData() function
	*/

	DIR*			pDir = NULL;
	struct dirent*	pDirEntry = NULL;
	int 			nRet = 0;
	
		
	if (scanOutboundFolderName == NULL)
	{
		return nRet;
	}

	printf("Scanning CSV data folder: %s...\n", scanOutboundFolderName);
	fflush(stdout);

	pDir = opendir(scanOutboundFolderName);
	if (pDir)
	{
		char	szDataFile[MAX_PATH];

		while ((pDirEntry = readdir(pDir)) != NULL)
		{
			if ( strcmp(pDirEntry->d_name, ".") && strcmp(pDirEntry->d_name, "..") )
			{
				sprintf(szDataFile, "%s/%s", scanOutboundFolderName, pDirEntry->d_name);
				nRet += parseData(szDataFile);
			}
		}

		closedir(pDir);
	}

	return nRet;
}

//-------------------------------------------------------------------------------------------------------
void* onSpooling(void *arg)
{
	/*
		This function (threaded, refer to main())
		performs the following actions on a regular basis, driven by g_SpoolingFrequency:
		  - scan the specified folder and look for all files in it
		  - for each file found, parse it in order to convert CSV data to MQTT data (filling data structures)
		  - convert data structure entries to MQTT payload
		  - publish the MQTT payload to AirVantage server
	*/

	ST_THREAD1_PARAM*	pstParam = (ST_THREAD1_PARAM*) arg;
	Client*				mqttClient = (Client*) pstParam->pMqttClient;
	char				szPayload[MAX_PAYLOAD_SIZE];

	while (!g_toStop)
	{
		//Spool the folder for CSV data files
		scanOutboundFolder(pstParam->szOutboundDataFolder);
		
		strcpy(szPayload, "[");	//start of payload
		int nPayloadCount = 0;

#if 1
		//Optimized version : JSON collection enabled, variable having the same key are gathered to reduce payload size

		int bDone = 0;

		do
		{
			int 			i, nCount;
			char			szKey[MAX_VALUE_LENGTH];
			unsigned long*	data_ts = NULL;
			void*			data = NULL;
			char**			data_values = NULL;
			char* 			data_payload;

			szKey[0] = 0;
			nCount = 0;

			for (i=0; i<g_nDataObjectCount; i++)
			{
				if (g_astDataObject[i].bProcessed == 0)
				{
					if (strlen(szKey) == 0)
					{
						//found the first unprocessed entry, nCount should be 0
						strcpy(szKey, g_astDataObject[i].szName);

						data = (void *) malloc(sizeof(void*));
						data_values = data;
						data_ts = (unsigned long *) malloc(sizeof(unsigned long));

						data_ts[nCount] = g_astDataObject[i].ulTimeStamp;
						data_values[nCount] = malloc(strlen(g_astDataObject[i].szValue)+1);
						strcpy(data_values[nCount], g_astDataObject[i].szValue);
						g_astDataObject[i].bProcessed = 1;
						nCount++;
					}
					else
					{
						if (strcmp(szKey, g_astDataObject[i].szName) == 0)
						{
							//found another unprocessed entry with the same Key
							data = (void *) realloc(data, (nCount+1)*sizeof(void*));
							data_values = data;
							data_ts = (unsigned long *) realloc(data_ts, (nCount+1) * sizeof(unsigned long));

							data_ts[nCount] = g_astDataObject[i].ulTimeStamp;
							data_values[nCount] = malloc(strlen(g_astDataObject[i].szValue)+1);
							strcpy(data_values[nCount], g_astDataObject[i].szValue);
							g_astDataObject[i].bProcessed = 1;
							nCount++;
						}
					}					
				}
			}

			if (data_ts == NULL)
			{
				//all entries have been processed
				bDone = 1;	//exit while loop
			}
			else if ( (strlen(szKey) > 0) && (data != NULL) && (data_ts != NULL) )
			{
				data_payload = swirjson_lstSerialize(szKey, nCount, data_values, data_ts);

				if (nPayloadCount > 0)
				{
					strcat(szPayload, ", ");
				}
				strcat(szPayload, data_payload);
				nPayloadCount++;
                                free(data_payload);
			}

			if (data_ts)
			{
				free(data_ts);
			}
			if (data)
			{
				free(data);
			}
		} while (!bDone);
#else
		//None optimized version : No JSON collection
		int nCount = 0;
		for (nCount=0; nCount<g_nDataObjectCount; nCount++)
		{
			unsigned long	data_ts[2];
			char*			data_values[2];
			int 			data_size = 0;
			char 			data_payload[MAX_PAYLOAD_SIZE];

			#if 0
			printf("preparing custom data %d of %d\n", nCount, g_nDataObjectCount);
			fflush(stdout);
			#endif
			data_ts[data_size] = g_astDataObject[nCount].ulTimeStamp;
			data_values[data_size] = malloc(strlen(g_astDataObject[nCount].szValue)+1);
			strcpy(data_values[data_size], g_astDataObject[nCount].szValue);
			data_size++;

			json_data(data_payload, g_astDataObject[nCount].szName, data_ts, data_values, data_size);
			if (nPayloadCount > 0)
			{
				strcat(szPayload, ", ");
			}
			strcat(szPayload, data_payload);
			nPayloadCount++;
		}
#endif

		strcat(szPayload, "]");	//end the paylpoad

		if ( nPayloadCount > 0 )
		{
			printf("[AVEP Data] %s\n", szPayload);

			MQTTMessage		msg;
			msg.qos = QOS0;
			msg.retained = 0;
			msg.dup = 0;
			msg.id = 0;
			msg.payload = szPayload;
			msg.payloadlen = strlen(szPayload);

			char* pTopic = malloc(strlen(TOPIC_NAME_PUBLISH) + strlen(g_szDeviceId) + 1);
			sprintf(pTopic, "%s%s", g_szDeviceId, TOPIC_NAME_PUBLISH);
			printf("Publish on %s\n", pTopic);
			fflush(stdout);
#ifndef OFFLINE
			int rc = MQTTPublish(mqttClient, pTopic, &msg);
			if(rc != 0)
			{
				printf("publish error: %d\n", rc);
				fflush(stdout);
			}
#endif
			if (pTopic)
			{
				free(pTopic);
			}
		}
		else
		{
			printf("No data to publish\n");
			fflush(stdout);
		}

		g_nDataObjectCount = 0;
		sleep(g_SpoolingFrequency);
	}
	return NULL;
}

//-------------------------------------------------------------------------------------------------------
int publishAckCmd(const char* szUid, int nAck, char* szMessage)
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

	printf("[ACK Message] %s\n", szPayload);

	MQTTMessage		msg;
	msg.qos = QOS0;
	msg.retained = 0;
	msg.dup = 0;
	msg.id = 0;
	msg.payload = szPayload;
	msg.payloadlen = strlen(szPayload);

	char* pTopic = malloc(strlen(TOPIC_NAME_ACK) + strlen(g_szDeviceId) + 1);
	sprintf(pTopic, "%s%s", g_szDeviceId, TOPIC_NAME_ACK);
	printf("Publish on %s\n", pTopic);
	fflush(stdout);
#ifndef OFFLINE
	Client*				mqttClient = (Client*) g_stSpoolingThreadParam.pMqttClient;
	int rc = MQTTPublish(mqttClient, pTopic, &msg);
	if (rc != 0)
	{
		printf("publish error: %d\n", rc);
		fflush(stdout);
	}
#endif
	if (pTopic)
	{
		free(pTopic);
	}
	if (szPayload)
	{
		free(szPayload);
	}

	return rc;
}

//-------------------------------------------------------------------------------------------------------
char* createFile()
{
	char*	szFilename = (char*) malloc(MAX_PATH);

	memset(szFilename, 0, MAX_PATH);

	time_t 		stime = time(NULL);
	struct tm*	ptm;
	char		szName[32];

	ptm = localtime(&stime);

	sprintf(szName, "data-%d%02d%02d-%02d%02d%02d",
			1900+ptm->tm_year,
			ptm->tm_mon+1,
			ptm->tm_mday,
			ptm->tm_hour,
			ptm->tm_min,
			ptm->tm_sec);

	int 	index = 0;
	
	do
	{
		sprintf(szFilename, "%s/%s-%02d.csv", g_szInboundDataFolder, szName, index++);

	} while (access(szFilename, F_OK) == 0);

	return szFilename;
}

//-------------------------------------------------------------------------------------------------------
void convertDataToCSV(char* szFilename, char* szKeyPath, char* szKey, char* szValue, char* szTimestamp)
{
	/*
		This function converts the provided JSON-decoded data (from MQTT payload) to CSV data (key;value;timestamp)
		Then saves the CSV data to file in the InboundDataFolder
	*/

	FILE*	file = NULL;
	char	szLine[MAX_PAYLOAD_SIZE];

	file = fopen(szFilename,"a");
	fseek(file, 0, SEEK_END);

	if (file == NULL)
	{
		printf("Failed to open %s\n", szFilename);
		perror("Error: ");
		fflush(stdout);
		return;
	}

	sprintf(szLine, ">> %s.%s;%s;%s... saved to file %s\n", szKeyPath, szKey, szValue, szTimestamp, szFilename);
	printf("%s", szLine);
	fflush(stdout);

	sprintf(szLine, "%s.%s;%s;%s\n", szKeyPath, szKey, szValue, szTimestamp);
	
	if (strlen(szLine) != fwrite(szLine, 1, strlen(szLine), file))
	{
		printf("Failed to write to file %s\n", szFilename);
		perror("Error: ");
		fflush(stdout);
	}

	fclose(file);
}

//-------------------------------------------------------------------------------------------------------
void onIncomingMessage(MessageData* md)
{
	/*
		This is a callback function (handler), invoked by MQTT client whenever there is an incoming message
		It performs the following actions :
		  - deserialize the incoming MQTT JSON-formatted message
		  - call convertDataToCSV()
	*/

	MQTTMessage* message = md->message;

	printf("\nIncoming data:\n%.*s%s\n", (int)message->payloadlen, (char*)message->payload, " ");

	char* szPayload = (char *) malloc(message->payloadlen+1);

	memcpy(szPayload, (char*)message->payload, message->payloadlen);
	szPayload[message->payloadlen] = 0;

	//decode JSON payload

	char* pszCommand = swirjson_getValue(szPayload, -1, "command");
	if (pszCommand)
	{
		char*	pszUid = swirjson_getValue(szPayload, -1, "uid");
		char*	pszTimestamp = swirjson_getValue(szPayload, -1, "timestamp");
		char*	pszId = swirjson_getValue(pszCommand, -1, "id");
		char*	pszParam = swirjson_getValue(pszCommand, -1, "params");

		char*	pszFilename = createFile();
                int     i;

		for (i=0; i<AV_JSON_KEY_MAX_COUNT; i++)
		{
			char szKey[AV_JSON_KEY_MAX_LENGTH];

			char * pszValue = swirjson_getValue(pszParam, i, szKey);

			if (pszValue)
			{
				convertDataToCSV(pszFilename, pszId, szKey, pszValue, pszTimestamp);
				free(pszValue);
			}
			else
			{
				break;
			}
		}

		publishAckCmd(pszUid, 0, "");

		if (pszFilename)
		{
			free(pszFilename);
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

	if (szPayload)
	{
		free(szPayload);
	}

	fflush(stdout);
}

//-------------------------------------------------------------------------------------------------------
int main(int argc, char** argv)
{
	/*
		Entry point of MQTTspooler :
		- Open a MQTT session on AirVantage server, with provided DeviceID (IMEI) and password
		- Subscribe to TOPIC_NAME_SUBSCRIBE with a provided callback function to process incoming MQTT message
		- Launch the background folder scanning function to scan the OutboundDataFolder (spooling thread)

		Mandatory ordered arguments:
			1. IMEI
			2. password (as registered on Airvantage, during Application Model assignment)
			3. Spooling directory name (outbound) : CVS to MQTT (for publishing)
			4. Inbound data directory name : Subcribed-topic MQTT incoming message to CSV
		Optional argument (5th):
			5. Spooling frequency in number of seconds
	*/

	int 			rc = 0;
	
	pthread_t 		pSpoolerThread;

	char			szServerUrl[] = URL_AIRVANTAGE_SERVER;
	int				nServerPortNumber = PORT_AIRVANTAGE_SERVER;

	Network 		oNetwork;
	Client 			mqttClient;
	int				nMaxRetry = 3;
	int				nRetry = 0;

	if (argc < 5)
	{
		printf("Usage: mqttspooler imei password scanOutboundFolder inboundDataFolder spoolingFrequency\n");
		return 1;
	}

	g_nDataObjectCount = 0;

	strcpy(g_szDeviceId, argv[1]);	//retrieve IMEI from cmd arg

	signal(SIGINT, onExit);
	signal(SIGTERM, onExit);

#ifndef OFFLINE
	for (nRetry=0; nRetry<nMaxRetry; nRetry++)
	{
		unsigned char	buf[MAX_PAYLOAD_SIZE];
		unsigned char	readbuf[MAX_PAYLOAD_SIZE];

		NewNetwork(&oNetwork);
		ConnectNetwork(&oNetwork, szServerUrl, nServerPortNumber);
		MQTTClient(&mqttClient, &oNetwork, TIMEOUT_MS, buf, sizeof(buf), readbuf, sizeof(readbuf));
	 
		MQTTPacket_connectData data = MQTTPacket_connectData_initializer;       
		data.willFlag = 0;
		data.MQTTVersion = MQTT_VERSION;
		data.clientID.cstring = g_szDeviceId;
		data.username.cstring = g_szDeviceId;
		data.password.cstring = argv[2];

		data.keepAliveInterval = 10000;
		data.cleansession = 1;
		printf("Attempting (%d/%d) to connect to tcp://%s:%d\n", nRetry+1, nMaxRetry, szServerUrl, nServerPortNumber);

		fflush(stdout);
	
		rc = MQTTConnect(&mqttClient, &data);
		printf("Connected %d\n", rc);
	    	fflush(stdout);

		if (rc == SUCCESS) 
		{
			//connected			
			break;
		}
		else
		{
			MQTTDisconnect(&mqttClient);
			oNetwork.disconnect(&oNetwork);
		}
	}

	if (rc != SUCCESS)
	{
		printf("Failed to connect to AirVantage server\n");
		fflush(stdout);
		return 1;
	}

	char* 	pTopic = malloc(strlen(g_szDeviceId) + strlen(TOPIC_NAME_SUBSCRIBE) + 1);
	sprintf(pTopic, "%s%s", g_szDeviceId, TOPIC_NAME_SUBSCRIBE);

	printf("Subscribing to %s\n", pTopic);
	rc = MQTTSubscribe(&mqttClient, pTopic, 0, onIncomingMessage);
	printf("Subscribed %d\n", rc);
	fflush(stdout);

#endif

	g_stSpoolingThreadParam.pMqttClient = &mqttClient;
	strcpy(g_stSpoolingThreadParam.szOutboundDataFolder, argv[3]);

	strcpy(g_szInboundDataFolder, argv[4]);

	if (argc > 5)
	{
		g_SpoolingFrequency = atol(argv[5]);
	}

	int err = pthread_create(&pSpoolerThread, NULL, &onSpooling, (void*) &g_stSpoolingThreadParam);

	if (err != 0)
	{
		printf("Can't start spooling thread (%s, %d)\n", strerror(err), err);
		fflush(stdout);
	}
	else
	{
		while (!g_toStop)
		{
#ifndef OFFLINE
			MQTTYield(&mqttClient, 1000);
#else
			sleep(1);
#endif
		}
	}
	
	printf("Exiting\n");

#ifndef OFFLINE
	MQTTDisconnect(&mqttClient);
	oNetwork.disconnect(&oNetwork);
#endif

	free(pTopic);

	return 0;
}


