/* A ~100-line RexxAE workload for timing parse + run on the ESP32-S3.
   Routines, SELECT, PARSE, string builtins, nested DO loops, a host call. */
SAY 'bench: start'
primes = ''
count = 0
DO n = 2 TO 120
  CALL isprime n
  IF RESULT = 1 THEN DO
    primes = primes n
    count = count + 1
  END
END
SAY 'bench: primes below 120 =' count
SAY 'bench: last five =' SUBWORD(primes, WORDS(primes) - 4)

fib_a = 0
fib_b = 1
DO k = 1 TO 25
  t = fib_a + fib_b
  fib_a = fib_b
  fib_b = t
END
SAY 'bench: fib(25) =' fib_a

sentence = 'the quick brown fox jumps over the lazy dog'
longest = ''
DO w = 1 TO WORDS(sentence)
  word = WORD(sentence, w)
  IF LENGTH(word) > LENGTH(longest) THEN longest = word
END
SAY 'bench: longest word =' longest
SAY 'bench: reversed =' REVERSE(sentence)
SAY 'bench: upper =' UPPER(sentence)
SAY 'bench: centred =' '['CENTER('aether', 12)']'
SAY 'bench: padded =' '['RIGHT(42, 6)']' '['LEFT('ab', 5)']'

line = 'temp=21.5;unit=C;sensor=board'
PARSE VAR line 'temp=' temp ';unit=' unit ';sensor=' sensor
SAY 'bench: parsed' temp unit sensor

grades = ''
DO score = 35 TO 100 BY 13
  CALL grade score
  grades = grades RESULT
END
SAY 'bench: grades =' STRIP(grades)

total = 0
DO i = 1 TO 20
  DO j = 1 TO 5
    total = total + i * j
  END
END
SAY 'bench: nested total =' total

doubled = 0
DO i = 1 TO 5
  doubled = doubled + DOUBLE(i)
END
SAY 'bench: host doubled sum =' doubled

s = ''
DO i = 1 TO 12
  m = i % 4
  s = s || COPIES('*', m)
END
SAY 'bench: stars =' LENGTH(s)

counter = 0
DO WHILE counter < 50
  counter = counter + 7
END
SAY 'bench: while ended at' counter

CALL gcd 1071, 462
SAY 'bench: gcd(1071, 462) =' RESULT
SAY 'bench: max/min =' MAX(3, 9, 4) MIN(3, 9, 4)
SAY 'bench: pos =' POS('fox', sentence) 'translate =' TRANSLATE('abc', 'xyz', 'abc')
SAY 'bench: done'
EXIT 0

isprime:
  PARSE ARG v
  IF v < 2 THEN RETURN 0
  d = 2
  DO WHILE d * d <= v
    IF v % d = 0 THEN RETURN 0
    d = d + 1
  END
  RETURN 1

grade:
  PARSE ARG g
  SELECT
    WHEN g >= 90 THEN RETURN 'A'
    WHEN g >= 75 THEN RETURN 'B'
    WHEN g >= 60 THEN RETURN 'C'
    OTHERWISE RETURN 'F'
  END

gcd:
  PARSE ARG a, b
  DO WHILE b <> 0
    r = a % b
    a = b
    b = r
  END
  RETURN a
