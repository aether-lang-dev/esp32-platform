/* Provisioned into script_a, not embedded in the firmware. */
start = UPTIME_MS()
CALL WAIT_MS 25
elapsed = UPTIME_MS() - start
IF elapsed < 25 THEN EXIT 1
SAY 'flash script resumed after timer'
CALL LOG_INFO 'runner script completed'
EXIT 0
