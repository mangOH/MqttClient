/*
 * swir_json.c
 *
 *	Helper class to deal with JSON payload serialization and deserialization
 *  getValue() method is generic (not specific to AV)... could be placed in a separate class
 *
 *  Created on: June 2015
 *      Author: Nhon Chu
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include <memory.h>

#include "json/swir_json.h"


#define JSON_MAX_PAYLOAD_SIZE	2048

#define JSON_KEY_VAL_SEPARATOR	':'
#define JSON_KEY_VAL_END_MARKER	','
#define JSON_QUOTE				'\"'
#define JSON_OBJECT_START		'{'
#define JSON_OBJECT_END			'}'
#define JSON_ARRAY_START		'['
#define JSON_ARRAY_END			']'


char* swirjson_szSerialize(const char* szKey, const char* szValue, unsigned long ulTimestamp)
{
	char	szPayload[JSON_MAX_PAYLOAD_SIZE];

	memset(szPayload, 0, sizeof(szPayload));

	if (ulTimestamp == 0)
	{
		//sprintf(szPayload, "[{\"%s\": [{\"timestamp\" : 1426939843000, \"value\" : \"%s\"}]}]", szKey, szValue);
		//sprintf(szPayload, "[{\"%s\": [{\"timestamp\" : \"\", \"value\" : \"%s\"}]}]", szKey, szValue);
		sprintf(szPayload, "{\"%s\":\"%s\"}", szKey, szValue);	
	}
	else
	{
		//sprintf(szPayload, "[{\"%s\": [{\"timestamp\" : %lu, \"value\" : \"%s\"}]}]", szKey, ulTimestamp, szValue);
		sprintf(szPayload, "{\"%lu\":{\"%s\":\"%s\"}}", ulTimestamp, szKey, szValue);
	}

	char*	pszJson = (char*) malloc(strlen(szPayload)+1);

	strcpy(pszJson, szPayload);

	return pszJson;
}

char* swirjson_fSerialize(char* szKey, float fValue, unsigned long ulTimestamp)
{
	char szValue[16];

	sprintf(szValue, "%.2f", fValue);

	return swirjson_szSerialize(szKey, szValue, ulTimestamp);
}

char* swirjson_nSerialize(char* szKey, int nValue, unsigned long ulTimestamp)
{
	char szValue[16];

	sprintf(szValue, "%d", nValue);

	return swirjson_szSerialize(szKey, szValue, ulTimestamp);
}

char* swirjson_lstSerialize(char* szKey, int nValueCount, char** pszValueList, unsigned long* pulTimestampList)
{
        char*	szPayload = (char *) malloc(JSON_MAX_PAYLOAD_SIZE);

	memset(szPayload, 0, JSON_MAX_PAYLOAD_SIZE);

        sprintf(szPayload, "{\"%s\": [", szKey);

        int i = 0;
        for (i=0; i<nValueCount; i++)
        {
                if (pulTimestampList == NULL)
                {
                        sprintf(szPayload, "%s {\"timestamp\" : \"\", \"value\" : \"%s\"}", szPayload, pszValueList[i]);
                }
                else if (pulTimestampList[i] == 0)
	        {
		        sprintf(szPayload, "%s {\"timestamp\" : \"\", \"value\" : \"%s\"}", szPayload, pszValueList[i]);
	        }
	        else
	        {
		        sprintf(szPayload, "%s {\"timestamp\" : %lu, \"value\" : \"%s\"}", szPayload, pulTimestampList[i], pszValueList[i]);
                }
                free(pszValueList[i]);
                
                if (i < nValueCount-1)
                {
                        strcat(szPayload, ",");
                }
        }

        strcat(szPayload, "]}");

        return szPayload;
}

char * swirjson_getValue(char* szJson, int nKeyIndex, char* szSearchKey)
{
	//use case 1 : nKeyIndex = -1 --> search by KeyName using szSearchKey as input
	//use case 2 : nKeyIndex > -1 --> search by index, szSearchKey, as output, will be filled with the keyName indexed by nIndexKey 
	char *	pszValue = NULL;

	char	cChar, cOpen, cClose;
	int		nState = 0, nPos = 0, nObjectCount = 0;
	int		nKeyNumber = -1;

	int		nKeyStartPos = -1, nKeyEndPos = -1;
	int 	nValStartPos = -1, nValEndPos = -1;

	do
	{
		cChar = szJson[nPos];

		switch (nState)
		{
			case 0:	//looks for open quote
			case 1: //looks for closing quote
				if (cChar == JSON_QUOTE)
				{
					//key : open and close quote
					if (nState == 0)
					{
						nKeyStartPos = nPos + 1;
					}
					else
					{
						nKeyEndPos = nPos - 1;
					}
					if (nKeyIndex==-1 && nState==1)
					{	//search with keyname
						if (nKeyStartPos > nKeyEndPos)
						{
							//no character in the quote/key/value
							nState = 0;	//skip this key/value, start over again and search for the next key
							break;
						}
						else if (0 != strncmp(szSearchKey, szJson+nKeyStartPos, nKeyEndPos - nKeyStartPos + 1))
						{
							nState = 0;	//skip this key/value, start over again and search for the next key
							break;
						}
					}
					nState++;
				}
				break;
			case 2:	//looks for separator, (only "white" spaces are allowed)
				if (cChar == JSON_KEY_VAL_SEPARATOR)
				{
					//found separator of value
					nState++;
					nValStartPos = nPos + 1;
				}
				else if (!isspace(cChar))
				{
					//error, unexpected character
					return pszValue;
				}
				break;
			case 3:	//detecting value starter
				if (cChar == JSON_QUOTE)
				{
					//found open quote of value
					nState = 4;
					nValStartPos = nPos + 1;
				}
				else if (cChar == JSON_KEY_VAL_END_MARKER)
				{
					//found end marker, no open quote. Case = "key" : xyz,
					nValEndPos = nPos - 1;
					nKeyNumber++;
					nState = -1;	//end search
				}
				else if (cChar == '\0')
				{
					//found end of string, no open quote. Case = "key" : xyz\0
					nValEndPos = nPos - 1;
					nKeyNumber++;
					nState = -1;	//end search
				}
				else if ((cChar == JSON_OBJECT_END) || (cChar == JSON_ARRAY_END))
				{
					//found end closing object/array, no open quote. Case = "key" : xyz}]
					nValEndPos = nPos - 1;
					nKeyNumber++;
					nState = -1;	//end search
				}
				else if ((cChar == JSON_OBJECT_START) || (cChar == JSON_ARRAY_START))
				{
					nState = 5;
					cOpen = cChar;
					cClose = (cChar == JSON_OBJECT_START ? JSON_OBJECT_END : JSON_ARRAY_END);
					nObjectCount = 1;
					nValStartPos = nPos + 1;
				}
				break;
			case 4:	//value started with a quote, now looks for ending quote
				if (cChar == JSON_QUOTE)
				{
					//found closing quote of the value. Case = "key" : "value"
					nValEndPos = nPos - 1;
					nKeyNumber++;
					nState = -1;	//end search
				}
				break;
			case 5: //value started with an object/array open marker, looks for closing marker
				if (cChar == cOpen)
				{
					nObjectCount++;
				}
				else if (cChar == cClose)
				{
					nObjectCount--;
					if (nObjectCount == 0)
					{
						//found closing quote of the value. Case = "key" : {...}
						nValEndPos = nPos - 1;
						nKeyNumber++;
						nState = -1;	//end search
					}
				}
				break;
			case -1:
				break;
		}

		int bFound = 0;

		if (nState == -1)
		{	
			//found key/value
			if (nKeyIndex == -1 && nKeyNumber > -1)
			{
				//search by KeyName
				bFound = 1;
			}
			else if (nKeyIndex > -1 && nKeyNumber == nKeyIndex)
			{
				//search by index
				bFound = 1;
			}
			nState = 0;	//ready for new search
		}

		if (bFound)
		{
			if (nValEndPos > nValStartPos)
			{
				int nLen = nValEndPos - nValStartPos + 1;
				pszValue = (char *) malloc(nLen + 1);
				memset(pszValue, 0, nLen + 1);
				memcpy(pszValue, szJson+nValStartPos, nLen);
			}
			else
			{
				pszValue = (char *) malloc(1);
				*pszValue = 0;
			}
			if (nKeyIndex > -1 && szSearchKey)
			{
				//return the KeyName
				memcpy(szSearchKey, szJson+nKeyStartPos, nKeyEndPos - nKeyStartPos + 1);
				szSearchKey[nKeyEndPos - nKeyStartPos + 1] = 0;
			}
			break;
		}

		nPos++;

	} while (nPos <= strlen(szJson));

	return pszValue;
}

