/* Firmware-owned LABELS service. Each command runs in a fresh context. */
PARSE VAR ADDRESS_COMMAND verb argument
SELECT
  WHEN UPPER(verb) = 'STATUS' & argument = '' THEN SAY 'PEER READY'
  WHEN UPPER(verb) = 'UPPER' THEN SAY UPPER(argument)
  OTHERWISE EXIT 4
END
EXIT 0
