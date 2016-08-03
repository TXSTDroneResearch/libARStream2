package com.parrot.arsdk.arstream2;

import com.parrot.mux.Mux;
import android.os.Process;
import android.util.Log;

public class ARStream2Manager
{
    private static final String TAG = ARStream2Manager.class.getSimpleName();
    private final long nativeRef;
    private final Thread networkThread;
    private final Thread outputThread;

    public ARStream2Manager(Mux mux, int maxPacketSize, int maxBitrate, int maxLatency, int maxNetworkLatency)
    {
        Mux.Ref muxRef = mux.newMuxRef();
        this.nativeRef = nativeMuxInit(muxRef.getCPtr(), maxPacketSize, maxBitrate, maxLatency, maxNetworkLatency);
        muxRef.release();
        this.networkThread = new Thread(new Runnable()
        {
            @Override
            public void run()
            {
                android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_DISPLAY);
                nativeRunNetworkThread(nativeRef);
            }
        }, "ARStream2Stream");
        this.outputThread = new Thread(new Runnable()
        {
            @Override
            public void run()
            {
                Process.setThreadPriority(Process.THREAD_PRIORITY_DISPLAY);
                nativeRunOutputThread(nativeRef);
            }
        }, "ARStream2Filter");
    }

    public ARStream2Manager(String serverAddress, int serverStreamPort, int serverControlPort, int clientStreamPort, int clientControlPort,
                         int maxPacketSize, int maxBitrate, int maxLatency, int maxNetworkLatency)
    {
        this.nativeRef = nativeNetInit(serverAddress, serverStreamPort, serverControlPort, clientStreamPort, clientControlPort,
                maxPacketSize, maxBitrate, maxLatency, maxNetworkLatency);
        this.networkThread = new Thread(new Runnable()
        {
            @Override
            public void run()
            {
                android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_DISPLAY);
                nativeRunNetworkThread(nativeRef);
            }
        }, "ARStream2Stream");
        this.outputThread = new Thread(new Runnable()
        {
            @Override
            public void run()
            {
                Process.setThreadPriority(Process.THREAD_PRIORITY_DISPLAY);
                nativeRunOutputThread(nativeRef);
            }
        }, "ARStream2Filter");
    }

    public boolean isValid()
    {
        return nativeRef != 0;
    }

    public void start()
    {
        if (isValid())
        {
            networkThread.start();
            outputThread.start();
        }
        else
        {
            Log.e(TAG, "unable to start, arstream2 manager is not valid! ");
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
                networkThread.join();
                outputThread.join();
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

    private native long nativeNetInit(String serverAddress, int serverStreamPort, int serverControlPort,
                                      int clientStreamPort, int clientControlPort,
                                      int maxPacketSize, int maxBitrate, int maxLatency,
                                      int maxNetworkLatency);

    private native long nativeMuxInit(long mux, int maxPacketSize, int maxBitrate, int maxLatency,
                                      int maxNetworkLatency);

    private native boolean nativeStop(long nativeRef);

    private native boolean nativeFree(long nativeRef);

    private native void nativeRunNetworkThread(long nativeRef);

    private native void nativeRunOutputThread(long nativeRef);
}
