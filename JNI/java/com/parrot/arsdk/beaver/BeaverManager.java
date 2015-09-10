package com.parrot.arsdk.beaver;


import android.os.Process;
import android.util.Log;

public class BeaverManager
{
    private static final String TAG = BeaverManager.class.getSimpleName();
    private final  long nativeRef;
    private final Thread streamThread;
    private final Thread controlThread;
    private final Thread filterThread;

    public BeaverManager(String serverAddress, int serverStreamPort, int serverControlPort, int clientStreamPort, int clientControlPort,
                         int maxPacketSize, int maxBitrate, int maxLatency, int maxNetworkLatency, int auFifoSize)
    {
        this.nativeRef = nativeInit(serverAddress, serverStreamPort, serverControlPort, clientStreamPort, clientControlPort,
                maxPacketSize, maxBitrate, maxLatency, maxNetworkLatency, auFifoSize);
        this.streamThread = new Thread(new Runnable()
        {
            @Override
            public void run()
            {
                android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_DISPLAY);
                nativeRunStreamThread(nativeRef);
            }
        }, "BeaverStream");
        this.controlThread = new Thread(new Runnable()
        {
            @Override
            public void run()
            {
                Process.setThreadPriority(Process.THREAD_PRIORITY_DISPLAY);
                nativeRunControlThread(nativeRef);
            }
        }, "BeaverControl");
        this.filterThread = new Thread(new Runnable()
        {
            @Override
            public void run()
            {
                Process.setThreadPriority(Process.THREAD_PRIORITY_DISPLAY);
                nativeRunFilterThread(nativeRef);
            }
        }, "BeaverFilter");
    }

    public boolean isValid()
    {
        return nativeRef != 0;
    }

    public void start()
    {
        if (isValid())
        {
            streamThread.start();
            controlThread.start();
            filterThread.start();
        }
        else
        {
            Log.e(TAG, "unable to start, beaver manager is not valid! ");
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
                streamThread.join();
                controlThread.join();
                filterThread.join();
            } catch (InterruptedException e)
            {
            }
            nativeFree(nativeRef);
        }
    }

    long getNativeRef()
    {
        return nativeRef;
    }

    private native long nativeInit(String serverAddress, int serverStreamPort, int serverControlPort,
                                   int clientStreamPort, int clientControlPort,
                                   int maxPacketSize, int maxBitrate, int maxLatency,
                                   int maxNetworkLatency, int auFifoSize);

    private native boolean nativeStop(long nativeRef);

    private native boolean nativeFree(long nativeRef);

    private native void nativeRunFilterThread(long nativeRef);

    private native void nativeRunStreamThread(long nativeRef);

    private native void nativeRunControlThread(long nativeRef);
}
