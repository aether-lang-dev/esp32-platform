# UIKit scroll controls crash on real taps before callbacks

Observed on iPhone15 Pro / iOS27.2, Device Console b011/b012, 2026-11-15.
Buttons inside scroll containers terminated the app before their callbacks.
Two retrieved reports show NSInvalidArgumentException (nil inserted into
NSMutableArray) in UIGestureRecognizer _delayTouchesForEvent:inPhase:,
through UIWindow sendEvent:. This was not a Rexx or TLS exception.

The UIKit backend built gesture-bearing views before UIApplicationMain.
The local aether-ui fix adds a process entry shim: UIApplicationMain starts
first, then didFinishLaunching invokes the renamed Aether main to construct
the tree. app_run_raw returns to the already-running UIKit lifecycle rather
than starting a second event loop. The phone packager and simulator CI use
the same entry shim. No desktop entry point or Rexx ABI changed.

Device Console b013 builds and runs using that startup path. On-phone geometry
shows the viewport361pt wide, editor/history337pt, with both toggle captions.
The phone returned42 and the S60 v6/floor6 APPS status over TLS. Paul then
physically tapped Check plug status and Run and confirmed the app stays open
and its results update. The final public console report is idle and includes42.

Existing UIKit Catalyst render/key/scroll-geometry tests pass, as do native
credential/input/duplicate-command checks. A reusable XCTest tap suite lives at
clients/native/ios/tests/touch/. It compiled, signed and launched its runner,
but this phone timed out enabling automation mode before tests ran. This is
not an automated tap PASS for that initial run; physical taps were verified by Paul.

Update2026-11-15 17:52: the retry on b014 successfully enabled automation and
passed the real-device XCTest suite (one test, zero failures). It tapped Status,
Run, both RC/RESULT switches, Cancel and Reset, verified the app stayed in the
foreground and that output included42. The previous automation blocker is gone.
