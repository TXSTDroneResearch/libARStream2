/**
 * @file BeaverReaderFilter.java
 * @brief H.264 Elementary Stream Reader and Filter
 * @date 08/04/2015
 * @author aurelien.barre@parrot.com
 */

package com.parrot.arsdk.beaver;

import com.parrot.arsdk.arsal.ARSALPrint;

import java.nio.ByteBuffer;

/**
 * Wrapper class for the BeaverReaderFilter C object.<br>
 * <br>
 * To create a BeaverReaderFilter, the application must provide a suitable
 * <code>BeaverReaderFilterListener</code> to handle the events.<br>
 * <br>
 */
public class BeaverReaderFilter
{
    private static final String TAG = BeaverReaderFilter.class.getSimpleName();

    /* *********************** */
    /* INTERNAL REPRESENTATION */
    /* *********************** */

    /**
     * Storage of the C pointer
     */
    final private long cReaderFilter;

    /**
     * Event listener
     */
    final private BeaverReaderFilterListener listener;

    /**
     * Check validity before all function calls
     */
    private boolean valid;

    private ByteBuffer[] buffers;

    /* *********** */
    /* CONSTRUCTOR */
    /* *********** */

    /**
     * Constructor for a BeaverReaderFilter object<br>
     *
     * @param auFifoSize         size of the access unit FIFO
     * @param waitForSync        wait for SPS/PPS sync before outputting access units
     * @param outputIncompleteAu output incomplete NAL units (with missing slices)
     * @param filterOutSpsPps    filter out SPS and PPS NAL units
     * @param filterOutSei       filter out SEI NAL units
     * @param listener           BeaverReaderFilter listener
     */
    public BeaverReaderFilter(String serverAddress, int serverStreamPort, int serverControlPort, int clientStreamPort, int clientControlPort,
                              int maxPacketSize, int maxBitrate, int maxLatency, int maxNetworkLatency, int auFifoSize,
                              BeaverReaderFilterListener listener)
    {
        this.listener = listener;
        this.cReaderFilter = nativeConstructor(serverAddress, serverStreamPort, serverControlPort, clientStreamPort, clientControlPort,
                                               maxPacketSize, maxBitrate, maxLatency, maxNetworkLatency, auFifoSize);
        this.valid = (this.cReaderFilter != 0);
    }

    /* ********** */
    /* DESTRUCTOR */
    /* ********** */

    /**
     * Destructor<br>
     * This destructor tries to avoid leaks if the object was not disposed
     */
    protected void finalize() throws Throwable
    {
        try
        {
            if (valid)
            {
                ARSALPrint.e(TAG, "Object " + this + " was not disposed !");
                stop();
                if (!dispose())
                {
                    ARSALPrint.e(TAG, "Unable to dispose object " + this + " ... leaking memory !");
                }
            }
        } finally
        {
            super.finalize();
        }
    }

    /* **************** */
    /* PUBLIC FUNCTIONS */
    /* **************** */

    /**
     * Checks if the current BeaverReaderFilter is valid.<br>
     * A valid BeaverReaderFilter is a BeaverReaderFilter which can be used to receive video frames.
     *
     * @return The validity of the BeaverReaderFilter.
     */
    public boolean isValid()
    {
        return this.valid;
    }

    /**
     * Beaver filter thread function
     */
    public void runFilter()
    {
        nativeRunFilterThread(cReaderFilter);
    }

    /**
     * Beaver stream thread function
     */
    public void runStream()
    {
        nativeRunStreamThread(cReaderFilter);
    }

    /**
     * Beaver control thread function
     */
    public void runControl()
    {
        nativeRunControlThread(cReaderFilter);
    }

    /**
     * Stops the internal threads of the BeaverReaderFilter.<br>
     * Calling this function allow the BeaverReaderFilter Runnables to end
     */
    public void stop()
    {
        nativeStop(cReaderFilter);
    }

    /**
     * Deletes the BeaverReaderFilter.<br>
     * This function should only be called after <code>stop()</code><br>
     * <br>
     * Warning: If this function returns <code>false</code>, then the BeaverReaderFilter was not deleted!
     *
     * @return <code>true</code> if the Runnables are not running.<br><code>false</code> if the BeaverReaderFilter could not be disposed now.
     */
    public boolean dispose()
    {
        boolean ret = nativeDispose(cReaderFilter);
        if (ret)
        {
            this.buffers = null;
            this.valid = false;
        }
        return ret;
    }

    /* ***************** */
    /* PACKAGE FUNCTIONS */
    /* ***************** */
    long getCReaderFilter()
    {
        return cReaderFilter;
    }

    /* ***************** */
    /* PRIVATE FUNCTIONS */
    /* ***************** */



    /**
     * spsPpsCallback wrapper for the listener
     */
    private int onSpsPpsReady(ByteBuffer sps, ByteBuffer pps)
    {
        try
        {
            this.buffers = listener.onSpsPpsReady(sps, pps);
        } catch (Throwable t)
        {
            ARSALPrint.e(TAG, "Exception in onSpsPpsReady" + t.getMessage());
            return -1;
        }
        if (this.buffers == null)
        {
            return -1;
        }
        return 0;
    }

    private int getFreeBufferIdx()
    {
        try
        {
            int bufferIdx = listener.getFreeBuffer();
            if (bufferIdx >= 0) {
                ARSALPrint.e(TAG, "\treturning index " + bufferIdx);
                return bufferIdx;
            }
            ARSALPrint.e(TAG, "\tNo more free buffers");
        }
        catch (Throwable t)
        {
            ARSALPrint.e(TAG, "Exception in getFreeBufferIdx" + t.getMessage());
        }
        return -1;
    }

    private ByteBuffer getBuffer(int bufferIdx)
    {
        try
        {
            return buffers[bufferIdx];
        }
        catch (Throwable t)
        {
            ARSALPrint.e(TAG, "Exception in getBuffer" + t.getMessage());
        }
        return null;
    }

    private int onBufferReady(int bufferIdx, int auSize, long auTimestamp, long auTimestampShifted, int iAuSyncType)
    {
        BEAVER_Filter_AuSyncType_t_ENUM auSyncType = BEAVER_Filter_AuSyncType_t_ENUM.getFromValue(iAuSyncType);
        if (auSyncType == null)
        {
            ARSALPrint.e(TAG, "Bad au sync type : " + iAuSyncType);
            return -1;
        }

        try
        {
            ByteBuffer buffer = this.buffers[bufferIdx];
            //buffer.limit(auSize);
            buffer.position(auSize);
            listener.onBufferReady(bufferIdx, auTimestamp, auTimestampShifted, auSyncType);
            return 0;
        } catch (Throwable t)
        {
            ARSALPrint.e(TAG, "Exception in onBufferReady" + t.getMessage());
            return -1;
        }
    }



    /* **************** */
    /* NATIVE FUNCTIONS */
    /* **************** */

    /**
     * Constructor in native memory space<br>
     * This function creates a C-Managed BeaverReaderFilter object
     *
     * @param serverAddress      server address
     * @param serverStreamPort   server stream port
     * @param serverControlPort  server control port
     * @param clientStreamPort   client stream port
     * @param clientControlPort  client control port
     * @param maxPacketSize      maximum network packet size
     * @param maxBitrate         maximum stream bitrate
     * @param maxLatency         maximum total latency
     * @param maxNetworkLatency  maximum natwork latency
     * @param auFifoSize         size of the access unit FIFO
     * @return C-Pointer to the BeaverReaderFilter object (or null if any error occured)
     */
    private native long nativeConstructor(String serverAddress, int serverStreamPort, int serverControlPort, int clientStreamPort, int clientControlPort,
                                          int maxPacketSize, int maxBitrate, int maxLatency, int maxNetworkLatency, int auFifoSize);

    /**
     * Entry point for the BeaverReaderFilter filter thread<br>
     * This function never returns until <code>stop</code> is called
     *
     * @param cReaderFilter C-Pointer to the BeaverReaderFilter C object
     */
    private native void nativeRunFilterThread(long cReaderFilter);

    /**
     * Entry point for the BeaverReaderFilter stream thread<br>
     * This function never returns until <code>stop</code> is called
     *
     * @param cReaderFilter C-Pointer to the BeaverReaderFilter C object
     */
    private native void nativeRunStreamThread(long cReaderFilter);

    /**
     * Entry point for the BeaverReaderFilter control thread<br>
     * This function never returns until <code>stop</code> is called
     *
     * @param cReaderFilter C-Pointer to the BeaverReaderFilter C object
     */
    private native void nativeRunControlThread(long cReaderFilter);

    /**
     * Stops the internal thread loops
     *
     * @param cReaderFilter C-Pointer to the BeaverReaderFilter C object
     */
    private native void nativeStop(long cReaderFilter);

    /**
     * Marks the BeaverReaderFilter as invalid and frees it if needed
     *
     * @param cReaderFilter C-Pointer to the BeaverReaderFilter C object
     */
    private native boolean nativeDispose(long cReaderFilter);

    /**
     * Initializes global static references in native code
     */
    private native static void nativeInitClass();

    /* *********** */
    /* STATIC BLOC */
    /* *********** */
    static
    {
        nativeInitClass();
    }
}
