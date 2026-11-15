import XCTest

final class ConsoleTouchTests: XCTestCase {
    func tap(_ title: String, in app: XCUIApplication) {
        let button = app.buttons[title]
        XCTAssertTrue(button.waitForExistence(timeout: 10), title)
        button.tap()
        XCTAssertEqual(app.state, .runningForeground, "\(title) terminated the app")
    }
    func edit(_ source: String, in app: XCUIApplication) {
        tap("Clear command", in: app)
        let editor = app.textViews.firstMatch
        editor.tap()
        editor.typeText(source)
        app.toolbars.buttons["Done"].tap()
    }
    func output(_ fragment: String, in app: XCUIApplication) {
        let text = app.staticTexts.matching(NSPredicate(format: "label CONTAINS %@", fragment)).firstMatch
        XCTAssertTrue(text.waitForExistence(timeout: 15), "Missing output: \(fragment)\n\(app.debugDescription)")
    }
    func testEverydayConsole() {
        continueAfterFailure = false
        let app = XCUIApplication(bundleIdentifier: "dev.aether.controller")
        app.launchEnvironment["CONTROLLER_LAYOUT_PROBE"] = "1"
        if app.state == .runningForeground || app.state == .runningBackground {
            app.activate()
        } else {
            app.launch()
        }
        XCTAssertTrue(app.staticTexts["Device Console · b017"].waitForExistence(timeout: 15))
        tap("Check plug status", in: app)
        output("Connected · app v6", in: app)
        tap("Reset session", in: app)
        tap("Clear output", in: app)
        edit("answer = 41\nSAY 'stored'", in: app)
        tap("Run", in: app)
        output("stored\n", in: app)

        // The device expires sessions after120 seconds idle. Leave the phone
        // untouched beyond that limit: foreground keepalive must preserve answer.
        let idle = expectation(description: "foreground session survives device idle limit")
        let idleSeconds = Double(ProcessInfo.processInfo.environment["CONSOLE_IDLE_SECONDS"] ?? "125") ?? 125
        DispatchQueue.main.asyncAfter(deadline: .now() + idleSeconds) { idle.fulfill() }
        wait(for: [idle], timeout: idleSeconds + 10)
        tap("Clear output", in: app)
        edit("SAY answer + 1", in: app)
        tap("Run", in: app)
        output("42\n", in: app)
        tap("Jump to latest", in: app)

        edit("unsent draft", in: app)
        tap("Previous", in: app)
        XCTAssertEqual(app.textViews.firstMatch.value as? String, "SAY answer + 1")
        tap("Previous", in: app)
        XCTAssertEqual(app.textViews.firstMatch.value as? String, "answer = 41\nSAY 'stored'")
        tap("Next", in: app)
        tap("Next", in: app)
        XCTAssertEqual(app.textViews.firstMatch.value as? String, "unsent draft")

        edit("SAY 'streaming'\nDO FOREVER\nCALL WAIT_MS 100\nEND", in: app)
        tap("Run", in: app)
        output("streaming\n", in: app)
        tap("Cancel", in: app)
        output("· cancelled", in: app)
        tap("Reset session", in: app)
        output("session closed", in: app)
        tap("Clear output", in: app)
        edit("SAY answer", in: app)
        tap("Run", in: app)
        output("ANSWER\n", in: app)
        tap("Reset session", in: app)
        output("session closed", in: app)
    }
}
