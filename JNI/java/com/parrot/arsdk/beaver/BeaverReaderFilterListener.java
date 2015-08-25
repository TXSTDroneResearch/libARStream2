/**
 * @file BeaverReaderFilterListener.java
 * @brief H.264 Elementary Stream Reader and Filter
 * @date 08/04/2015
 * @author aurelien.barre@parrot.com
 */

package com.parrot.arsdk.beaver;


import java.nio.ByteBuffer;

/**
 * This interface describes a listener for BeaverReaderFilter events
 */
public interface BeaverReaderFilterListener
{
    /**
     * Called when sps/pps are received
     * 
     * Implementation init the decoder and return an array of codec input buffers that will be filled with received AU
     */
    ByteBuffer[] onSpsPpsReady(ByteBuffer sps, ByteBuffer pps);

    /**
     * Called when a new input buffer is require
     * 
     * Implementation must return the index of one of the available input buffer, or -1 if there is no available input buffer
     * The buffer is considered busy until
     */
    int getFreeBuffer();

    /**
     * Called when a buffer is ready to be sent to the decoder
     * 
     */
    void onBufferReady(int bufferIdx, long auTimestamp, long auTimestampShifted, BEAVER_Filter_AuSyncType_t_ENUM auSyncType);
}

