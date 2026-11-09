/* Signed CYD demo. The firmware owns the simulated load and deadline. */
ADDRESS SETTINGS 'get auto_off_seconds'
IF RC <> 0 THEN EXIT RC
seconds = RESULT
on_command = 'on ' || seconds
on_label = 'ON ' || seconds || ' SEC'
button = BUTTON_EVENT()
IF button = JSON_PARSE('null') THEN SAY 'SWITCH TIMEOUT' seconds
denied = 0
IF button = 1 THEN ADDRESS SWITCH on_command
IF button = 2 THEN ADDRESS SWITCH 'off'
IF button = 1 | button = 2 THEN DO
  IF RC = 13 THEN denied = 1
  ELSE IF RC <> 0 THEN EXIT RC
END
ADDRESS SWITCH 'status'
IF RC <> 0 THEN EXIT RC
message = 'LOAD' RESULT
IF denied = 1 THEN message = 'DENIED'
SAY message
CALL DISPLAY_FRAME 'TIMED SWITCH', message
CALL DISPLAY_BUTTON 1, on_label, 2, 4, 11, 4
CALL DISPLAY_BUTTON 2, 'OFF', 2, 10, 11, 4
EXIT 0
