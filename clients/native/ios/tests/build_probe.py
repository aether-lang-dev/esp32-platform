"""Build the native transport probe against the same pinned TLS source as iOS."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[4]
tls = Path(os.environ["AETHER_TLS_DIR"]).resolve()
expected = (root/"clients/native/ios/AETHER_TLS_REF").read_text().strip()
assert subprocess.check_output(["git", "-C", str(tls), "rev-parse", "HEAD"], text=True).strip() == expected
subprocess.run(["git", "-C", str(tls), "diff", "--quiet", "HEAD", "--",
                "std/cryptography/tls13_client/module.ae", "std/cryptography/tls13_ks/module.ae"], check=True)
ae = os.environ.get("AE", "ae")
output = Path(os.environ.get("CONTROLLER_PROBE_OUTPUT", "/tmp/controller-pure-psk-probe"))
with tempfile.TemporaryDirectory() as directory:
    scratch = Path(directory)
    subprocess.run([ae, "build", str(root/"clients/native/ios/transport.ae"), "--lib", str(tls),
                    "--emit=csrc", "--with=fs,net,os,extern", "-o", str(scratch/"ControllerTLS")], cwd=tls, check=True)
    flags = shlex.split(subprocess.check_output([ae, "cflags"], text=True))
    source = root/"clients/native/ios"
    subprocess.run(["clang", "-O1", "-fobjc-arc", "-fblocks", *flags,
                    "-I"+str(scratch), "-I"+str(source), str(scratch/"ControllerTLS.c"),
                    str(source/"ControllerCredential.m"), str(source/"ControllerTransport.m"),
                    str(source/"ControllerConsole.m"), str(source/"tests"/os.environ.get("CONTROLLER_PROBE_SOURCE", "transport_probe.m")), "-framework", "Foundation", "-framework", "Security",
                    "-o", str(output)], check=True)
print("Built native probe:", output)
