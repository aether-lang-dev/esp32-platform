/* FNK0114B signed USB app. Grant TOUCH_EVENT and DISPLAY_BUTTONS. */
button = BUTTON_EVENT()
message = 'Tap a button'
IF button = 1 THEN message = 'Hello, CYD!'
IF button = 2 THEN message = 'Rexx on ESP32'
IF button <> JSON_PARSE('null') THEN SAY 'CYD button' button message
CALL DISPLAY_FRAME 'REXXAE TOUCH', message
CALL DISPLAY_BUTTON 1, 'HELLO', 1, 4, 6, 3
CALL DISPLAY_BUTTON 2, 'ABOUT', 4, 9, 7, 3
CALL DISPLAY_BUTTON 3, 'RESET', 9, 13, 5, 3
EXIT 0
