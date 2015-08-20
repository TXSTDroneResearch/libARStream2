/**
 * @file BeaverReaderFilterListener.java
 * @brief H.264 Elementary Stream Reader and Filter
 * @date 08/04/2015
 * @author aurelien.barre@parrot.com
 */

package com.parrot.beaver;

import com.parrot.arsdk.arsal.ARNativeData;

/**
 * This interface describes a listener for BeaverReaderFilter events
 */
public interface BeaverReaderFilterListener
{
     void onSpsPpsReady(long spsBuffer, int spsSize, long ppsBuffer, int ppsSize);

     void onGetAuBuffer();

     void onCancelAuBuffer(long auBuffer, int auBufferSize);

     void onAuReady(long auBuffer, int auSize, long auTimestamp, long auTimestampShifted, BEAVER_FILTER_AU_SYNC_TYPE_ENUM auSyncType);
}

