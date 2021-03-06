//--------------------------------------------------------------------------------------------------
/**
 * @file dataHub.c
 *
 * Data Hub component initializer and utilities shared by other modules.
 *
 * The Resource Tree structure and Namespaces are implemented by the resTree module.
 *
 * The Resource base class and Placeholder resource are implemented by the resource module
 * (prefix = res_).
 *
 * Inputs and Outputs are implemented by the ioRes module.
 *
 * Observations are implemented by the obs module.
 *
 * Data Samples are implemented by the dataSample module.
 *
 * Copyright (C) Sierra Wireless Inc.
 */
//--------------------------------------------------------------------------------------------------

#include "legato.h"
#include "interfaces.h"
#include "dataHub.h"
#include "nan.h"
#include "dataSample.h"
#include "handler.h"
#include "resource.h"
#include "resTree.h"
#include "ioPoint.h"
#include "obs.h"
#include "ioService.h"
#include "adminService.h"
#include "snapshot.h"
#include "configService.h"


//--------------------------------------------------------------------------------------------------
/**
 * Component initializer.
 */
//--------------------------------------------------------------------------------------------------
#ifndef UNIT_TEST
COMPONENT_INIT
#else
void initDataHub(void)
#endif
{
    dataSample_Init();
    handler_Init();
    res_Init();
    ioPoint_Init();
    obs_Init();
    resTree_Init();
    ioService_Init();
    adminService_Init();
    snapshot_Init();

    LE_INFO("Data Hub started.");
}


//--------------------------------------------------------------------------------------------------
/**
 * Get a printable string name for a given data type (e.g., "numeric").
 *
 * @return Pointer to the name.
 */
//--------------------------------------------------------------------------------------------------
const char* hub_GetDataTypeName
(
    io_DataType_t type
)
//--------------------------------------------------------------------------------------------------
{
    switch (type)
    {
        case IO_DATA_TYPE_TRIGGER:
            return "trigger";

        case IO_DATA_TYPE_BOOLEAN:
            return "Boolean";

        case IO_DATA_TYPE_NUMERIC:
            return "numeric";

        case IO_DATA_TYPE_STRING:
            return "string";

        case IO_DATA_TYPE_JSON:
            return "JSON";
    }

    LE_FATAL("Unknown data type %d.", type);
}


//--------------------------------------------------------------------------------------------------
/**
 * Get a printable string name for a given resource tree entry type (e.g., "observation").
 *
 * @return Pointer to the name.
 */
//--------------------------------------------------------------------------------------------------
const char* hub_GetEntryTypeName
(
    admin_EntryType_t type
)
//--------------------------------------------------------------------------------------------------
{
    switch (type)
    {
        case ADMIN_ENTRY_TYPE_NONE:
            return "** none **";

        case ADMIN_ENTRY_TYPE_NAMESPACE:
            return "namespace";

        case ADMIN_ENTRY_TYPE_PLACEHOLDER:
            return "placeholder";

        case ADMIN_ENTRY_TYPE_INPUT:
            return "input";

        case ADMIN_ENTRY_TYPE_OUTPUT:
            return "output";

        case ADMIN_ENTRY_TYPE_OBSERVATION:
            return "observation";
    }

    LE_FATAL("Unknown entry type %d.", type);
}

//--------------------------------------------------------------------------------------------------
/**
 * Set the client app's namespace.
 *
 * @return
 *  - LE_OK if setting client's namespace was successful.
 *  - LE_DUPLICATE if namespace has already been set.
 */
//--------------------------------------------------------------------------------------------------
le_result_t hub_SetClientNamespace
(
    le_msg_SessionRef_t sessionRef,  ///< [IN] IPC session reference.
    const char* appNamespace         ///< [IN] namespace
)
//--------------------------------------------------------------------------------------------------
{
    if (le_msg_GetSessionContextPtr(sessionRef) != NULL)
    {
        return LE_DUPLICATE;
    }

    resTree_EntryRef_t nsRef;
    // Get the "/app" namespace first.
    LE_ASSERT(resTree_GetEntry(resTree_GetRoot(), "app", &nsRef) == LE_OK);
    // Now get the app's namespace under the /app namespace.
    LE_ASSERT(resTree_GetEntry(nsRef, appNamespace, &nsRef) == LE_OK);

    le_mem_AddRef(nsRef);

    // TODO: Need to remove this and instead release nsRef in client disconnection handler.
#if LE_CONFIG_LINUX
    LE_FATAL("Not Permitted on Linux");
#endif

    // Store the namespace entry reference as the IPC session Context Ptr
    le_msg_SetSessionContextPtr(sessionRef, nsRef);
    return LE_OK;
}

//--------------------------------------------------------------------------------------------------
/**
 * Get the client app's namespace.
 *
 * @return the reference to the namespace resource tree entry or NULL if failed.
 */
//--------------------------------------------------------------------------------------------------
resTree_EntryRef_t hub_GetClientNamespace
(
    le_msg_SessionRef_t sessionRef  ///< [IN] IPC session reference.
)
//--------------------------------------------------------------------------------------------------
{
    resTree_EntryRef_t nsRef;

    nsRef = le_msg_GetSessionContextPtr(sessionRef);
    if (nsRef != NULL)
    {
        return nsRef;
    }

    // Get the client app name.
    pid_t clientPid;
    le_result_t result = le_msg_GetClientProcessId(sessionRef, &clientPid);
    if (result != LE_OK)
    {
        LE_KILL_CLIENT("Unable to retrieve client process ID (%s)", LE_RESULT_TXT(result));
        return NULL;
    }
    char appName[LE_LIMIT_APP_NAME_LEN + 1];
    result = le_appInfo_GetName(clientPid, appName, sizeof(appName));
    if (result != LE_OK)
    {
        LE_KILL_CLIENT("Unable to retrieve client app name (%s)", LE_RESULT_TXT(result));
        return NULL;
    }
    // Get the "/app" namespace first.
    LE_ASSERT(resTree_GetEntry(resTree_GetRoot(), "app", &nsRef) == LE_OK);
    // Now get the app's namespace under the /app namespace.
    LE_ASSERT(resTree_GetEntry(nsRef, appName, &nsRef) == LE_OK);
    // Store the namespace entry reference as the IPC session Context Ptr to speed things up
    // next time.
    le_msg_SetSessionContextPtr(sessionRef, nsRef);
    return nsRef;
}

//--------------------------------------------------------------------------------------------------
/**
 *  Allocate memory from pool
 *
 *  @return
 *      Pointer to the allocated object or NULL if failed to allocate.
 */
//--------------------------------------------------------------------------------------------------
void* hub_MemAlloc
(
    le_mem_PoolRef_t    pool    ///< [IN] Pool from which the object is to be allocated.
)
//--------------------------------------------------------------------------------------------------
{
#if LE_CONFIG_LINUX
    return le_mem_Alloc(pool);
#else
    return le_mem_TryAlloc(pool);
#endif
}

//--------------------------------------------------------------------------------------------------
/**
 *  Is resource Path malformed?
 *
 *  @return
 *      true If path is malformed.
 *      false is path is valid.
 */
//--------------------------------------------------------------------------------------------------
bool hub_IsResourcePathMalformed
(
    const char* path
)
//--------------------------------------------------------------------------------------------------
{
    const char* illegalCharPtr = strpbrk(path, ".[]");
    if (illegalCharPtr != NULL)
    {
        LE_ERROR("Illegal character '%c' in path '%s'.", *illegalCharPtr, path);
        return true;
    }
    size_t i = 0;   // Index into path.

    while (path[i] != '\0' && i < HUB_MAX_RESOURCE_PATH_BYTES)
    {
        // If we're at a slash, skip it.
        if (path[i] == '/')
        {
            i++;
        }

        // Look for a slash or the end of the string as the terminator of the next entry name.
        const char* terminatorPtr = strchrnul(path + i, '/');

        // Compute the length of the entry name in this path element.
        size_t nameLen = terminatorPtr - (path + i);

        // Sanity check the length.
        if (nameLen == 0)
        {
            LE_ERROR("Resource path element missing in path '%s'.", path);
            return true;
        }
        if (nameLen >= HUB_MAX_ENTRY_NAME_BYTES)
        {
            LE_ERROR("Resource path element too long in path '%s'.", path);
            return true;
        }

        // Advance the index past the name.
        i += nameLen;
    }
    return false;
}
