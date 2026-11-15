"""XCTest real-touch regression against the already-installed phone app."""
from pathlib import Path
import os
import subprocess
import tempfile

here = Path(__file__).resolve().parent
team = os.environ['IOS_TEAM']
device = os.environ['IOS_DEVICE']
idle_seconds = float(os.environ.get('CONSOLE_IDLE_SECONDS', '125'))
assert 0 <= idle_seconds <= 180
with tempfile.TemporaryDirectory(prefix='controller-touch-') as folder:
    root = Path(folder)
    project = root/'Touch.xcodeproj'
    (project/'xcshareddata/xcschemes').mkdir(parents=True)
    (root/'ConsoleTouchTests.swift').write_text((here/'ConsoleTouchTests.swift').read_text())
    (project/'project.pbxproj').write_text('''// !$*UTF8*$!
{ archiveVersion=1; classes={}; objectVersion=56; objects={
 A00000000000000000000001={isa=PBXFileReference; lastKnownFileType=sourcecode.swift; path=ConsoleTouchTests.swift; sourceTree="<group>";};
 A00000000000000000000002={isa=PBXBuildFile; fileRef=A00000000000000000000001;};
 A00000000000000000000003={isa=PBXFileReference; explicitFileType=wrapper.cfbundle; path=TouchTests.xctest; sourceTree=BUILT_PRODUCTS_DIR;};
 A00000000000000000000004={isa=PBXGroup; children=(A00000000000000000000001,A00000000000000000000005); sourceTree="<group>";};
 A00000000000000000000005={isa=PBXGroup; children=(A00000000000000000000003); name=Products; sourceTree="<group>";};
 A00000000000000000000006={isa=PBXSourcesBuildPhase; buildActionMask=2147483647; files=(A00000000000000000000002); runOnlyForDeploymentPostprocessing=0;};
 A00000000000000000000007={isa=PBXNativeTarget; buildConfigurationList=A00000000000000000000009; buildPhases=(A00000000000000000000006); buildRules=(); dependencies=(); name=TouchTests; productName=TouchTests; productReference=A00000000000000000000003; productType="com.apple.product-type.bundle.ui-testing";};
 A00000000000000000000008={isa=PBXProject; attributes={LastUpgradeCheck=1600;}; buildConfigurationList=A0000000000000000000000A; compatibilityVersion="Xcode 14.0"; developmentRegion=en; knownRegions=(en,Base); mainGroup=A00000000000000000000004; productRefGroup=A00000000000000000000005; projectDirPath=""; projectRoot=""; targets=(A00000000000000000000007);};
 A00000000000000000000009={isa=XCConfigurationList; buildConfigurations=(A0000000000000000000000B); defaultConfigurationIsVisible=0; defaultConfigurationName=Debug;};
 A0000000000000000000000A={isa=XCConfigurationList; buildConfigurations=(A0000000000000000000000C); defaultConfigurationIsVisible=0; defaultConfigurationName=Debug;};
 A0000000000000000000000B={isa=XCBuildConfiguration; name=Debug; buildSettings={SDKROOT=iphoneos; IPHONEOS_DEPLOYMENT_TARGET=17.0; PRODUCT_NAME=TouchTests; PRODUCT_BUNDLE_IDENTIFIER=dev.aether.controller.touch; GENERATE_INFOPLIST_FILE=YES; SWIFT_VERSION=5.0; CODE_SIGN_STYLE=Automatic; DEVELOPMENT_TEAM=TEAM_ID; TARGETED_DEVICE_FAMILY="1,2";};};
 A0000000000000000000000C={isa=XCBuildConfiguration; name=Debug; buildSettings={SDKROOT=iphoneos; IPHONEOS_DEPLOYMENT_TARGET=17.0;};};
}; rootObject=A00000000000000000000008;}
'''.replace('TEAM_ID',team))
    (project/'xcshareddata/xcschemes/Touch.xcscheme').write_text('''<?xml version="1.0" encoding="UTF-8"?>
<Scheme version="1.7"><BuildAction><BuildActionEntries><BuildActionEntry buildForTesting="YES"><BuildableReference BuildableIdentifier="primary" BlueprintIdentifier="A00000000000000000000007" BuildableName="TouchTests.xctest" BlueprintName="TouchTests" ReferencedContainer="container:Touch.xcodeproj"/></BuildActionEntry></BuildActionEntries></BuildAction><TestAction buildConfiguration="Debug"><EnvironmentVariables><EnvironmentVariable key="CONSOLE_IDLE_SECONDS" value="IDLE_SECONDS" isEnabled="YES"/></EnvironmentVariables><Testables><TestableReference skipped="NO"><BuildableReference BuildableIdentifier="primary" BlueprintIdentifier="A00000000000000000000007" BuildableName="TouchTests.xctest" BlueprintName="TouchTests" ReferencedContainer="container:Touch.xcodeproj"/></TestableReference></Testables></TestAction></Scheme>
'''.replace('IDLE_SECONDS',str(idle_seconds)))
    result = os.environ.get('TOUCH_RESULT', '/tmp/controller-touch.xcresult')
    subprocess.run(['xcodebuild','-project',str(project),'-scheme','Touch','-destination','id='+device,
                    '-derivedDataPath',str(root/'dd'),'-allowProvisioningUpdates','-allowProvisioningDeviceRegistration',
                    '-resultBundlePath',result,'test'],check=True,timeout=300)
