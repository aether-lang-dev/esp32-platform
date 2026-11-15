import XCTest

final class ConsoleTouchTests: XCTestCase {
    func testButtonsDoNotTerminateApp() {
        continueAfterFailure = false
        let app = XCUIApplication(bundleIdentifier: "dev.aether.controller")
        app.launchEnvironment["CONTROLLER_LAYOUT_PROBE"] = "1"
        app.launch()
        XCTAssertTrue(app.buttons["Check plug status"].waitForExistence(timeout: 15))
        app.buttons["Check plug status"].tap()
        XCTAssertEqual(app.state, .runningForeground, "Status tap terminated the app")
        let connected = app.staticTexts.matching(NSPredicate(format: "label CONTAINS %@", "Connected · app v6")).firstMatch
        XCTAssertTrue(connected.waitForExistence(timeout: 15), app.debugDescription)
        app.buttons["Run"].tap()
        XCTAssertEqual(app.state, .runningForeground, "Run tap terminated the app")
        let output = app.staticTexts.matching(NSPredicate(format: "label CONTAINS %@", "42\n")).firstMatch
        XCTAssertTrue(output.waitForExistence(timeout: 15), app.debugDescription)
        app.switches["Show RC"].tap()
        app.switches["Show RESULT"].tap()
        XCTAssertEqual(app.state, .runningForeground, "Toggle tap terminated the app")
        app.buttons["Cancel"].tap()
        app.buttons["Reset session"].tap()
        XCTAssertEqual(app.state, .runningForeground, "Reset tap terminated the app")
    }
}
