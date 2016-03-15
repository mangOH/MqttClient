/**
 * This module receives incoming MQTT messages (sent by AirVantage).
 *
 * Copyright (C) Sierra Wireless Inc. Use of this work is subject to license.
 *
 */

/* Legato Framework */
#include "legato.h"



void ProcessCommand
(
    const char*  szPath,        ///< [IN] data path
    const char*  szKey,         ///< [IN] data key
    const char*  szValue,       ///< [IN] data value
    const char*  szTimestamp    ///< [IN] data timestamp
)
{
    LE_INFO("\n*** received AV command ***");

    char    szDisplayMsg[256];

    sprintf(szDisplayMsg, "%s.%s = %s @ %s", szPath, szKey, szValue, szTimestamp);
    LE_INFO("%s\n", szDisplayMsg);
}


void receiverApi_Notify
(
    const char*  szPath,        ///< [IN] data path
    const char*  szKey,         ///< [IN] data key
    const char*  szValue,       ///< [IN] data value
    const char*  szTimestamp    ///< [IN] data timestamp
)
{
    LE_INFO("receiver API: Received AV Command...");
    ProcessCommand(szPath, szKey, szValue, szTimestamp);
}



//--------------------------------------------------------------------------------------------------
/**
 *  Main function.
 */
//--------------------------------------------------------------------------------------------------
COMPONENT_INIT
{
    LE_INFO("MQTT receiver launched");
}

