/**
 * @file arstream2_error.c
 * @brief Parrot Streaming Library - Error definitions
 * @date 04/16/2015
 * @author aurelien.barre@parrot.com
 */

#include <libARStream2/arstream2_error.h>

char* ARSTREAM2_Error_ToString(eARSTREAM2_ERROR error)
{
    switch (error)
    {
    case ARSTREAM2_OK:
        return "no error";
        break;
    case ARSTREAM2_ERROR_BAD_PARAMETERS:
        return "bad parameters";
        break;
    case ARSTREAM2_ERROR_ALLOC:
        return "unable to allocate resource";
        break;
    case ARSTREAM2_ERROR_BUSY:
        return "object is busy and can not be deleted yet";
        break;
    case ARSTREAM2_ERROR_QUEUE_FULL:
        return "queue is full";
        break;
    case ARSTREAM2_ERROR_WAITING_FOR_SYNC:
        return "waiting for synchronization";
        break;
    case ARSTREAM2_ERROR_RESYNC_REQUIRED:
        return "re-synchronization required";
        break;
    case ARSTREAM2_ERROR_RESOURCE_UNAVAILABLE:
        return "resource unavailable";
        break;
    case ARSTREAM2_ERROR_NOT_FOUND:
        return "not found";
        break;
    case ARSTREAM2_ERROR_INVALID_STATE:
        return "invalid state";
        break;
    case ARSTREAM2_ERROR_UNSUPPORTED:
        return "unsupported";
        break;
    default:
        return "unknown error";
        break;
    }
    return "unknown error";
}
