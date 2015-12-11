/**
 * @file arstream2_error.c
 * @brief Parrot Streaming Library - Error definitions
 * @date 04/16/2015
 * @author aurelien.barre@parrot.com
 */

#include <libBeaver/beaver_error.h>

char* ARSTREAM2_Error_ToString(eARSTREAM2_ERROR error)
{
    switch (error)
    {
    case ARSTREAM2_OK:
        return "No error";
        break;
    case ARSTREAM2_ERROR_BAD_PARAMETERS:
        return "Bad parameters";
        break;
    case ARSTREAM2_ERROR_ALLOC:
        return "Unable to allocate data";
        break;
    case ARSTREAM2_ERROR_BUSY:
        return "Object is busy and can not be deleted yet";
        break;
    case ARSTREAM2_ERROR_QUEUE_FULL:
        return "Queue is full";
        break;
    default:
        return "Unknown value";
        break;
    }
    return "Unknown value";
}
