//--------------------------------------------------------------------------------------------------
/**
 * @file sender.c
 *
 * This component is used to start/stop mqttClient and to send mqtt messages to AirVantage.
 *
 * <hr>
 *
 * Copyright (C) Sierra Wireless Inc. Use of this work is subject to license.
 *
 */
//--------------------------------------------------------------------------------------------------

#include "legato.h"
#include "interfaces.h"


//--------------------------------------------------------------------------------------------------
/**
 * Helper.
 *
 */
//--------------------------------------------------------------------------------------------------
static void PrintUsage()
{
    int     idx;
    bool    sandboxed = (getuid() != 0);
    const   char * usagePtr[] =
            {
                "Usage of the 'mqttSender' tool is:",
                "   send <Key> <Value>"
            };

    for (idx = 0; idx < NUM_ARRAY_MEMBERS(usagePtr); idx++)
    {
        if(sandboxed)
        {
            LE_INFO("%s", usagePtr[idx]);
        }
        else
        {
            fprintf(stderr, "%s\n", usagePtr[idx]);
        }
    }
}

//--------------------------------------------------------------------------------------------------
/**
 * App init.
 *
 */
//--------------------------------------------------------------------------------------------------
COMPONENT_INIT
{
    if (le_arg_NumArgs() == 2)
    {
        LE_INFO("Calling mqttClient to publish message");
        int32_t     nErrCode = 0;
        mqtt_Send(le_arg_GetArg(0), le_arg_GetArg(1), &nErrCode);

        if (nErrCode == 0)
        {
            LE_INFO("Publish message OK");
        }
        else
        {
            LE_INFO("Publish message FAILED");
        }

        exit(EXIT_SUCCESS);
    }
    else
    {
        PrintUsage();
        exit(EXIT_FAILURE);
    }
}
