package com.parrot.arsdk.arstream2;

import android.os.Process;
import android.util.Log;

public class ARStream2Resender
{
    private static final String TAG = ARStream2Resender.class.getSimpleName();
    private final long nativeRef;
    private final Thread resenderThread;

    public ARStream2Resender(ARStream2Manager manager, String clientAddress,
                          int serverStreamPort, int serverControlPort,
                          int clientStreamPort, int clientControlPort,
                          int maxPacketSize, int targetPacketSize, int maxLatency, int maxNetworkLatency)
    {
        this.nativeRef = nativeInit(manager.getNativeRef(), clientAddress,
                serverStreamPort, serverControlPort,
                clientStreamPort, clientControlPort,
                maxPacketSize, targetPacketSize, maxLatency, maxNetworkLatency);
        this.resenderThread = new Thread(new Runnable()
        {
            @Override
            public void run()
            {
                Process.setThreadPriority(Process.THREAD_PRIORITY_DISPLAY);
                nativeRunThread(nativeRef);
            }
        }, "ARStream2Resender");
    }

    private boolean isValid()
    {
        return nativeRef != 0;
    }

    public void start()
    {
        if (isValid())
        {
            resenderThread.start();
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
            nativeStop(nativeRef);
        }
    }

    public void dispose()
    {
        if (isValid())
        {
            try
            {
                resenderThread.join();
            } catch (InterruptedException e)
            {
            }
            nativeFree(nativeRef);
        }
    }

    private native long nativeInit(long arstream2ManagerNativeRef, String clientAddress,
                                   int serverStreamPort, int serverControlPort,
                                   int clientStreamPort, int clientControlPort,
                                   int maxPacketSize, int targetPacketSize, int maxLatency, int maxNetworkLatency);

    private native boolean nativeStop(long nativeRef);

    private native boolean nativeFree(long nativeRef);

    private native void nativeRunThread(long nativeRef);

}
