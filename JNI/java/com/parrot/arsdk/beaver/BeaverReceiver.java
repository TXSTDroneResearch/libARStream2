package com.parrot.arsdk.beaver;

import android.util.Log;

import com.parrot.arsdk.arsal.ARSALPrint;

import java.nio.ByteBuffer;

public class BeaverReceiver
{
    private static final String TAG = BeaverReceiver.class.getSimpleName();

    private final long beaverManagerNativeRef;
    private final long nativeRef;
    private final BeaverReceiverListener listener;
    private ByteBuffer[] buffers;

    public BeaverReceiver(BeaverManager manager, BeaverReceiverListener listener)
    {
        this.listener = listener;
        this.beaverManagerNativeRef = manager.getNativeRef();
        this.nativeRef = nativeInit();
    }

    public boolean isValid()
    {
        return beaverManagerNativeRef != 0;
    }

    public void start()
    {
        if (isValid())
        {
            nativeStart(beaverManagerNativeRef, nativeRef);
        }
        else
        {
            Log.e(TAG, "unable to start, resender is not valid! ");
        }

    }

    public void stop()
    {
        if (isValid())
        {
            nativeStop(beaverManagerNativeRef);
        }
    }

    public void dispose()
    {
        buffers = null;
        nativeFree(nativeRef);
    }

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
        if (this.buffers != null)
        {
            return 0;
        }
        return -1;
    }

    private int getFreeBufferIdx()
    {
        try
        {
            int bufferIdx = listener.getFreeBuffer();
            if (bufferIdx >= 0)
            {
                return bufferIdx;
            }
            ARSALPrint.e(TAG, "\tNo more free buffers");
        } catch (Throwable t)
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
        } catch (Throwable t)
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
        }
        return -1;
    }

    private native long nativeInit();
    private native void nativeFree(long nativeRef);
    private native boolean nativeStart(long beaverManagerNativeRef, long nativeRef);
    private native boolean nativeStop(long beaverManagerNativeRef);
    private native static void nativeInitClass();
    static
    {
        nativeInitClass();
    }
 }
