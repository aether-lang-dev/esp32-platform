"""Use the upstream UIKit packager with this app's native adapters, in local scratch."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
ui = Path(os.environ.get("AETHER_UI_DIR", str(root.parent/"aether-ui"))).resolve()
out = root/"build/native-ios"
out.mkdir(parents=True, exist_ok=True)
if not os.environ.get("AETHER_TLS_DIR"):
    raise SystemExit("Set AETHER_TLS_DIR to the tested local external-PSK Aether checkout")
tls_source = Path(os.environ["AETHER_TLS_DIR"]).resolve()
expected = (root/"clients/native/ios/AETHER_TLS_REF").read_text().strip()
actual = subprocess.check_output(["git", "-C", str(tls_source), "rev-parse", "HEAD"], text=True).strip()
if actual != expected:
    raise SystemExit("AETHER_TLS_DIR does not match the tested AETHER_TLS_REF pin")
subprocess.run(["git", "-C", str(tls_source), "diff", "--quiet", "HEAD", "--",
                "std/cryptography/tls13_client/module.ae", "std/cryptography/tls13_ks/module.ae"], check=True)
core = out/"transport"
core.mkdir(exist_ok=True)
subprocess.run([os.environ.get("AE", "ae"), "build", str(root/"clients/native/ios/transport.ae"),
                "--lib", str(tls_source), "--emit=csrc", "--with=fs,net,os,extern",
                "-o", str(core/"ControllerTLS")], cwd=tls_source, check=True)
source = (ui/"tools/ios-app.sh").read_text()

def replace(old, new):
    global source
    assert old in source, "Upstream UIKit packager changed; review integration"
    source = source.replace(old, new, 1)

replace('ROOT="$(cd "$(dirname "$0")/.." && pwd)"', 'ROOT="$AETHER_UI_DIR"')
replace('OUT="$ROOT/target/ios/$APP"', 'OUT="$CONTROLLER_BUILD/app"')
replace('RUNTIME="$ROOT/target/ios/libaether_ios.a"', 'RUNTIME="$CONTROLLER_BUILD/libaether_ios.a"')
replace('"$ROOT/backend/aether_ui_uikit.m" \\', '"$CONTROLLER_BUILD/transport/ControllerTLS.c" \\\n         "$CONTROLLER_SOURCE/ios/ControllerCredential.m" \\\n         "$CONTROLLER_SOURCE/ios/ControllerTransport.m" \\\n         "$CONTROLLER_SOURCE/ios/ControllerActions.m" \\\n         "$CONTROLLER_SOURCE/ios/ControllerHistory.m" \\\n         "$CONTROLLER_SOURCE/ios/ControllerConsole.m" \\\n         "$CONTROLLER_SOURCE/ios/ControllerPlatform.m" \\\n         "$ROOT/backend/aether_ui_uikit.m" \\')
replace('-I"$ROOT/backend"', '-I"$ROOT/backend" -I"$CONTROLLER_BUILD/transport"')
replace('-framework UserNotifications -lm"', '-framework UserNotifications -framework Network -framework Security -lm"')
replace('<key>CFBundleDisplayName</key><string>$APP</string>', '<key>CFBundleDisplayName</key><string>Device Console</string>')
replace('<key>LSRequiresIPhoneOS</key><true/>', '<key>NSLocalNetworkUsageDescription</key><string>Connect directly to your smart plug using its local credential.</string>\n    <key>LSRequiresIPhoneOS</key><true/>')
# C/ObjC sources are inputs, not resources; never bundle development files.
replace('< <(find "$APP_DIR" -type f ! -name \'*.ae\' ! -name \'.*\' ! -path \'*/.*\' -size -8M | sort)', '< <(printf "")')
env = {**os.environ, "AETHER_UI_DIR":str(ui), "CONTROLLER_BUILD":str(out),
       "CONTROLLER_SOURCE":str(root/"clients/native"), "IOS_BUNDLE_ID":"dev.aether.controller"}
with tempfile.TemporaryDirectory() as scratch:
    script = Path(scratch)/"ios-app.sh"
    script.write_text(source)
    subprocess.run(["bash", str(script), str(root/"clients/native/app.ae")], env=env, check=True)
