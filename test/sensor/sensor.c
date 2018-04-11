#include "legato.h"
#include "interfaces.h"

static double TimerPeriod = 0;

static le_timer_Ref_t Timer;

//--------------------------------------------------------------------------------------------------
/**
 * Function called when the timer expires.
 */
//--------------------------------------------------------------------------------------------------
static void TimerExpired
(
    le_timer_Ref_t timer
)
//--------------------------------------------------------------------------------------------------
{
    static double counter = 0;

    counter++;

    io_PushNumeric("counter", 0.0, counter);

    // On the 3rd push, do some additional testing.
    if (counter == 3.0)
    {
        LE_INFO("Running create/delete tests");

        // Create the Input with different type and units and expect LE_DUPLICATE.
        le_result_t result = io_CreateInput("counter", IO_DATA_TYPE_STRING, "count");
        LE_ASSERT(result == LE_DUPLICATE);
        result = io_CreateInput("counter", IO_DATA_TYPE_NUMERIC, "s");
        LE_ASSERT(result == LE_DUPLICATE);
        result = io_CreateOutput("counter", IO_DATA_TYPE_NUMERIC, "count");
        LE_ASSERT(result == LE_DUPLICATE);

        // Create the Input with same type and units and expect LE_OK.
        result = io_CreateInput("counter", IO_DATA_TYPE_NUMERIC, "count");
        LE_ASSERT(result == LE_OK);

        // Delete the Input and re-create it.
        io_DeleteResource("counter");
        result = io_CreateInput("counter", IO_DATA_TYPE_NUMERIC, "count");
        LE_ASSERT(result == LE_OK);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Call-back function called when an update is received from the Data Hub for the "period"
 * config setting.
 */
//--------------------------------------------------------------------------------------------------
static void PeriodUpdateHandler
(
    double timestamp,
    double value,
    void* contextPtr    ///< not used
)
//--------------------------------------------------------------------------------------------------
{
    LE_INFO("Received update to 'period' setting: %lf (timestamped %lf)", value, timestamp);

    TimerPeriod = value;

    le_timer_SetMsInterval(Timer, (uint32_t)(value * 1000));

    // If the timer is not running, start it now.
    if (!le_timer_IsRunning(Timer))
    {
        le_timer_Start(Timer);
    }
}


COMPONENT_INIT
{
    le_result_t result;

    // This will be provided to the Data Hub.
    result = io_CreateInput("counter", IO_DATA_TYPE_NUMERIC, "count");
    LE_ASSERT(result == LE_OK);

    // This is my configuration setting.
    result = io_CreateOutput("period", IO_DATA_TYPE_NUMERIC, "s");
    LE_ASSERT(result == LE_OK);

    // Register for notification of updates to our configuration setting.
    io_AddNumericPushHandler("period", PeriodUpdateHandler, NULL);

    // Create a repeating timer that will call TimerExpired() each time it expires.
    // Note: we'll start the timer when we receive our configuration setting.
    Timer = le_timer_Create("counter");
    le_timer_SetRepeat(Timer, 0);
    le_timer_SetHandler(Timer, TimerExpired);
}