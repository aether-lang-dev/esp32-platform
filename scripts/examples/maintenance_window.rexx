/* Schedule from a fixed UTC instant. The runner currently has monotonic
   uptime, so this example supplies its own wall-clock input. */
schedule = '0 30 2 * * 1-5'
reference = '2026-11-06T02:30:00Z'
IF CRON_VALID(schedule) <> 1 THEN EXIT 1
nextWindow = CRON_NEXT(schedule, reference)
IF nextWindow <> '2026-11-09T02:30:00.000Z' THEN EXIT 1
CALL LOG_DEBUG 'maintenance schedule evaluated'
CALL LOG_INFO ('Next maintenance window: ' || nextWindow)
EXIT 0
