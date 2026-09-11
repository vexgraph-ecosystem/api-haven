package api;

import annotation.Draft;
import annotation.Intention;
import exception.APIException;
import net.HTTPClient;
import net.JSON;
import nio.ForeignMemory;
import primitive.string;
import telemetry.CrashDumper;

/**
 * High-performance, zero-allocation API Client for the Vex ecosystem.
 * Formulates off-heap JSON packets and transmits them over native sockets.
 */
@Draft
@Intention("Direct libcurl API transmission module pushing telemetry status from off-heap segments")
public final class APIClient
{
    private APIClient() {}

    /**
     * Gathers live engine telemetry off-heap, builds a JSON string, and POSTs it.
     * Triggers local telemetry exception dumping and throws APIException if transmission fails.
     */
    public static void sendTelemetry(String endpointUrl)
    {
        if (endpointUrl == null || endpointUrl.isEmpty())
        {
            throw new APIException("Endpoint URL cannot be null or empty.");
        }

        // 1. Gather raw telemetry metrics from the off-heap block
        long frame = ForeignMemory.getLong(CrashDumper.TELEMETRY_BLOCK + CrashDumper.OFFSET_FRAME);
        int state = ForeignMemory.getInt(CrashDumper.TELEMETRY_BLOCK + CrashDumper.OFFSET_STATE);
        int pipelines = ForeignMemory.getInt(CrashDumper.TELEMETRY_BLOCK + CrashDumper.OFFSET_ACTIVE_PIPELINES);
        long allocs = ForeignMemory.getLong(CrashDumper.TELEMETRY_BLOCK + CrashDumper.OFFSET_ALLOC_COUNT);

        // 2. Build JSON package off-heap (0% Java GC)
        long obj = JSON.createObject();
        obj = JSON.putInt(obj, "state", state);
        obj = JSON.putLong(obj, "frameCount", frame);
        obj = JSON.putInt(obj, "activePipelines", pipelines);
        obj = JSON.putLong(obj, "totalAllocations", allocs);

        long jsonStrPtr = JSON.build(obj); // Returns null-terminated off-heap string pointer

        long resPtr = 0L;
        try
        {
            // 3. Post telemetry payload using FFM libcurl HTTP client
            resPtr = HTTPClient.post(endpointUrl, string.get(jsonStrPtr));
            if (resPtr == 0L)
            {
                throw new APIException("HTTP POST request failed: libcurl return descriptor is NULL.");
            }

            System.out.println("[API Client] Telemetry transmitted successfully. Response: " + string.get(resPtr));
        }
        catch (Throwable t)
        {
            // 4. In case of failure, dump exception details directly to local crash dump
            CrashDumper.dumpException(t);
            throw new APIException("Failed to transmit telemetry to remote endpoint: " + endpointUrl, t);
        }
        finally
        {
            // 5. Clean up off-heap memory allocations
            string.free(jsonStrPtr);
            if (resPtr != 0L)
            {
                string.free(resPtr);
            }
        }
    }
}
