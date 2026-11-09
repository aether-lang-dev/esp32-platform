/* Copyright (C) 2021 Toitware ApS.
 * Use of this source code is governed by the Zero-Clause BSD license
 * in LICENSE in this directory.
 * Adapted from Toit examples/deep_sleep.toit.
 */
cause = WAKE_CAUSE()
IF cause = 1 THEN SAY 'Woke from the timer'
ELSE SAY 'Started for another reason'
SAY 'Awake for' UPTIME_MS() 'ms this boot'
SAY 'Sleeping for 10 seconds'
EXIT 0
