/**
* MIT License
*
* Copyright (c) 2018 Infineon Technologies AG
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE
*
* \file
*
* \brief   This file implements
*
* \addtogroup  grOptigaUtil
* @{
*/

#include "optiga/optiga_util.h"
#include "optiga/comms/optiga_comms.h"
#include "optiga/cmd/CommandLib.h"
#include "optiga/pal/pal_os_timer.h"

///Length of metadata
#define LENGTH_METADATA             0x1C

volatile static host_lib_status_t optiga_comms_status;

#ifdef MODULE_ENABLE_READ_WRITE

static void __optiga_util_comms_event_handler(void* upper_layer_ctx, host_lib_status_t event)
{
    optiga_comms_status = event;
}

optiga_lib_status_t optiga_util_open_application(optiga_comms_t* p_comms)
{
    optiga_lib_status_t status = OPTIGA_LIB_ERROR;
    sOpenApp_d sOpenApp;

    do {
        // OPTIGA(TM) Initialization phase
        //Invoke optiga_comms_open to initialize the IFX I2C Protocol and security chip
        optiga_comms_status = OPTIGA_COMMS_BUSY;
        p_comms->upper_layer_handler = __optiga_util_comms_event_handler;
        status = optiga_comms_open(p_comms);
        if(E_COMMS_SUCCESS != status)
        {
            status = OPTIGA_LIB_ERROR;
            break;
        }

        //Wait until IFX I2C initialization is complete
        while(optiga_comms_status == OPTIGA_COMMS_BUSY)
        {
            pal_os_timer_delay_in_milliseconds(1);
        }

        if((OPTIGA_COMMS_SUCCESS != status) || (optiga_comms_status == OPTIGA_COMMS_ERROR))
        {
            status = OPTIGA_LIB_ERROR;
            break;
        }

        //Set OPTIGA comms context in Command library before invoking the use case APIs or command library APIs
        //This context will be used by command library to communicate with OPTIGA using IFX I2C Protocol.
        CmdLib_SetOptigaCommsContext(p_comms);

        //Open the application in Security Chip
        sOpenApp.eOpenType = eInit;
        status = CmdLib_OpenApplication(&sOpenApp);
        if(CMD_LIB_OK == status)
        {
            status = OPTIGA_LIB_SUCCESS;
        }
    } while(FALSE);

    return status;
}

optiga_lib_status_t optiga_util_read_data(uint16_t optiga_oid, uint16_t offset,
                                          uint8_t * p_buffer, uint16_t* buffer_size)
{
    int32_t status  = (int32_t)OPTIGA_LIB_ERROR;
    sGetData_d cmd_params;
    sCmdResponse_d cmd_resp;

    do
    {
        if((NULL == p_buffer) || (NULL == buffer_size) || (0 == *buffer_size))
        {
            status = (int32_t)OPTIGA_LIB_ERROR;
            break;
        }

        cmd_params.wOID = optiga_oid;
        cmd_params.wLength = *buffer_size;
        cmd_params.wOffset = offset;
        cmd_params.eDataOrMdata = eDATA;

        cmd_resp.prgbBuffer = p_buffer;
        cmd_resp.wBufferLength = *buffer_size;
        cmd_resp.wRespLength = 0;

        status = CmdLib_GetDataObject(&cmd_params,&cmd_resp);

        if(CMD_LIB_OK != status)
        {
            break;
        }
        *buffer_size = cmd_resp.wRespLength;
        status = OPTIGA_LIB_SUCCESS;
    }while(FALSE);

    return status;
}

optiga_lib_status_t optiga_util_read_metadata(uint16_t optiga_oid, uint8_t * p_buffer, uint16_t* buffer_size)
{
    int32_t status  = (int32_t)OPTIGA_LIB_ERROR;
    sGetData_d cmd_params;
    sCmdResponse_d cmd_resp;
    uint16_t buffer_limit = *buffer_size;

    do
    {
        if((NULL == p_buffer) || (NULL == buffer_size) || (0 == *buffer_size))
        {
            status = (int32_t)OPTIGA_LIB_ERROR;
            break;
        }

        //Get metadata of OID
        cmd_params.wOID = optiga_oid;
        cmd_params.wLength = LENGTH_METADATA;
        cmd_params.wOffset = 0;
        cmd_params.eDataOrMdata = eMETA_DATA;

        cmd_resp.prgbBuffer = p_buffer;
        cmd_resp.wBufferLength = buffer_limit;
        cmd_resp.wRespLength = 0;

        status = CmdLib_GetDataObject(&cmd_params,&cmd_resp);
        if(CMD_LIB_OK != status)
        {
            break;
        }
        *buffer_size = cmd_resp.wRespLength;
        status = OPTIGA_LIB_SUCCESS;

    }while(FALSE);

    return status;

}

optiga_lib_status_t optiga_util_write_data(uint16_t optiga_oid, uint8_t write_type, uint16_t offset, uint8_t * p_buffer, uint16_t buffer_size)
{
    int32_t status  = (int32_t)OPTIGA_LIB_ERROR;

    sSetData_d sd_params;

    do
    {
        if((NULL == p_buffer) || (0x00 == buffer_size))
        {
            break;
        }

        if ((OPTIGA_UTIL_WRITE_ONLY != write_type) && (OPTIGA_UTIL_ERASE_AND_WRITE != write_type))
        {
            status = OPTIGA_UTIL_ERROR_INVALID_INPUT;
            break;
        }

        sd_params.wOID = optiga_oid;
        sd_params.wOffset = offset;
        sd_params.eDataOrMdata = eDATA;
        if (OPTIGA_UTIL_ERASE_AND_WRITE == write_type)
        {
            sd_params.eWriteOption = eERASE_AND_WRITE;
        }
        else
        {
            sd_params.eWriteOption = eWRITE;
        }
        sd_params.prgbData = p_buffer;
        sd_params.wLength = buffer_size;

        status = CmdLib_SetDataObject(&sd_params);
        if(CMD_LIB_OK != status)
        {
            break;
        }
        status = OPTIGA_LIB_SUCCESS;
    }while(FALSE);

    return status;
}

optiga_lib_status_t optiga_util_write_metadata(uint16_t optiga_oid, uint8_t * p_buffer, uint8_t buffer_size)
{

    int32_t status  = (int32_t)OPTIGA_LIB_ERROR;

    sSetData_d sd_params;

    sd_params.wOID = optiga_oid;
    sd_params.wOffset = 0;
    sd_params.eDataOrMdata = eMETA_DATA;
    sd_params.eWriteOption = eWRITE;
    sd_params.prgbData = p_buffer;
    sd_params.wLength = buffer_size;

    status = CmdLib_SetDataObject(&sd_params);
    if(CMD_LIB_OK != status)
    {
        return  status;
    }

    return OPTIGA_LIB_SUCCESS;
}

#endif // MODULE_ENABLE_READ_WRITE
