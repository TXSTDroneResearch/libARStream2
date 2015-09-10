package com.parrot.arsdk.beaver;

import android.os.Process;
import android.util.Log;

public class BeaverResender
{
    private static final String TAG = BeaverResender.class.getSimpleName();
    private final long nativeRef;
    private final Thread streamThread;
    private final Thread controlThread;

    public BeaverResender(BeaverManager manager, String clientAddress,
                          int serverStreamPort, int serverControlPort,
                          int clientStreamPort, int clientControlPort,
                          int maxPacketSize, int targetPacketSize, int maxLatency, int maxNetworkLatency)
    {
        this.nativeRef = nativeInit(manager.getNativeRef(), clientAddress,
                serverStreamPort, serverControlPort,
                clientStreamPort, clientControlPort,
                maxPacketSize, targetPacketSize, maxLatency, maxNetworkLatency);
        this.streamThread = new Thread(new Runnable()
        {
            @Override
            public void run()
            {
                Process.setThreadPriority(Process.THREAD_PRIORITY_DISPLAY);
                nativeRunStreamThread(nativeRef);
            }
        }, "BeaverResenderStream");
        this.controlThread = new Thread(new Runnable()
        {
            @Override
            public void run()
            {
                Process.setThreadPriority(Process.THREAD_PRIORITY_DISPLAY);
                nativeRunControlThread(nativeRef);
            }
        }, "BeaverResenderControl");
    }

    private boolean isValid()
    {
        return nativeRef != 0;
    }

    public void start()
    {
        if (isValid())
        {
            streamThread.start();
            controlThread.start();
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
                streamThread.join();
                controlThread.join();
            } catch (InterruptedException e)
            {
            }
            nativeFree(nativeRef);
        }
    }

    private native long nativeInit(long beaverReaderNativeRef, String clientAddress,
                                   int serverStreamPort, int serverControlPort,
                                   int clientStreamPort, int clientControlPort,
                                   int maxPacketSize, int targetPacketSize, int maxLatency, int maxNetworkLatency);

    private native boolean nativeStop(long nativeRef);

    private native boolean nativeFree(long nativeRef);

    private native void nativeRunStreamThread(long nativeRef);

    private native void nativeRunControlThread(long nativeRef);

}
