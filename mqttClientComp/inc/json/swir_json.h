/*
 * swir_json.h
 *
 *	Helper class to deal with JSON payload serialization and deserialization
 *  getValue() method is generic (not specific to AV)... could be placed in a separate class
 *
 *  Created on: June 2015
 *      Author: Nhon Chu
 */

#ifndef _SWIR_JSON_H_
#define _SWIR_JSON_H_



char*		swirjson_szSerialize(const char* szKey, const char* szValue, unsigned long ulTimestamp);
char*		swirjson_fSerialize(char* szKey, float fValue, unsigned long ulTimestamp);
char*		swirjson_nSerialize(char* szKey, int nValue, unsigned long ulTimestamp);
char*		swirjson_lstSerialize(char* szKey, int nValueCount, char** pszValueList, unsigned long* pulTimestampList);
char *		swirjson_getValue(char* szJson, int nKeyIndex, char* szSearchKey);


#endif	//_SWIR_JSON_H_
