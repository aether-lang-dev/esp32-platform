/* Signed maintenance app. It needs no SWITCH grant. */
button = BUTTON_EVENT()
IF button = 1 THEN ADDRESS SETTINGS 'set auto_off_seconds 10'
IF button = 2 THEN ADDRESS SETTINGS 'set auto_off_seconds 5'
IF button = 1 | button = 2 THEN DO
  IF RC <> 0 THEN EXIT RC
END
ADDRESS SETTINGS 'get auto_off_seconds'
IF RC <> 0 THEN EXIT RC
message = 'TIMEOUT ' || RESULT || 'S'
SAY message
CALL DISPLAY_FRAME 'SWITCH CONFIG', message
CALL DISPLAY_BUTTON 1, 'SET 10 SEC', 2, 4, 11, 4
CALL DISPLAY_BUTTON 2, 'RESET TO 5', 2, 10, 11, 4
EXIT 0
