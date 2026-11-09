import ctypes as c
import sys

class Limits(c.Structure):
    _fields_=[('memory_bytes',c.c_uint64),('deadline_ms',c.c_int64),('deadline_counts',c.c_int),('parent',c.c_void_p)]
if len(sys.argv)!=2: raise SystemExit("usage: probe_rexxae_resources.py /absolute/path/to/librexxae")
lib=c.CDLL(sys.argv[1])
lib.aether_exec_account_new.argtypes=[c.POINTER(Limits)];lib.aether_exec_account_new.restype=c.c_void_p
lib.aether_exec_use.argtypes=[c.c_void_p,c.c_void_p];lib.aether_exec_use.restype=c.c_int
lib.aether_exec_account_used.argtypes=[c.c_void_p];lib.aether_exec_account_used.restype=c.c_uint64
lib.aether_exec_account_free.argtypes=[c.c_void_p]
lib.aether_rexxae_create.argtypes=[c.c_char_p];lib.aether_rexxae_create.restype=c.c_void_p
lib.aether_rexxae_step.argtypes=[c.c_void_p,c.c_int];lib.aether_rexxae_step.restype=c.c_int
lib.aether_rexxae_destroy.argtypes=[c.c_void_p]
lib.aether_last_outcome.restype=c.c_int
lib.aether_last_outcome_text.restype=c.c_char_p

sources={
 'builtin': ('SAY LENGTH(COPIES("'+'x'*1024+'",300))\nEXIT 0\n').encode(),
 'parse': ("x=1\n"*1800+"EXIT 0\n").encode(),
}
# Warm process-level lazy state outside any account.
warm=lib.aether_rexxae_create(b"EXIT 0\n");lib.aether_rexxae_destroy(warm)
failures=[]
for label,source in sources.items():
 for cap in [1,8,32,64,96,128,256,512,1024,2048,4096,8192,16384,32768,65536,100000,131072]:
  account=lib.aether_exec_account_new(c.byref(Limits(cap,0,0,None)));assert account
  assert lib.aether_exec_use(account,None)==0
  ctx=lib.aether_rexxae_create(source);outcome=lib.aether_last_outcome()
  if ctx and not outcome:
   for _ in range(1000):
    status=lib.aether_rexxae_step(ctx,32);outcome=lib.aether_last_outcome()
    if outcome or status<=0:break
  assert lib.aether_exec_use(None,None)==0
  if ctx:lib.aether_rexxae_destroy(ctx)
  held=lib.aether_exec_account_used(account);lib.aether_exec_account_free(account)
  if held:failures.append((label,cap,outcome,held))
print('Sweep retained allocations:',failures)
raise SystemExit(1 if failures else 0)
