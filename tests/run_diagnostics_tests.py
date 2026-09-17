"""Keep intentional crash cases independent and reject incomplete QtTest output."""
import pathlib
import subprocess
import sys
import tempfile

cases = ["filteringAndPrivacy", "boundedConcurrentQueueAndRotation", "breadcrumbsWithoutOrdinaryLogs",
         "writeFailureDoesNotCrash", "cleanupProtectsActiveSession", "zipExportAndCancellation",
         "killedAndDisabled", "bothDisabledAndLateEnable", "expiredSessionsAndTimeRange",
         "snapshotExcludesAnotherLiveSession", "disabledCrashProducesNoDump", "timeWindowKeepsLongRunningSession",
         "nestedContextIsRedacted", "oversizedContextIsBounded", "controllerRejectsUnsafeExportTargets",
         "controllerScansOffTheCallingThread", "crashHelperLossIsReported"]
cases += ["crashCapture:" + mode + suffix for mode in ["access", "throw", "abort", "fatal"]
          for suffix in ["", "-worker"]]
with tempfile.TemporaryDirectory() as temporary:
    for case in cases:
        report = pathlib.Path(temporary) / "result.txt"
        result = subprocess.run([sys.argv[1], case, "-o", str(report) + ",txt"], timeout=30)
        text = report.read_text(encoding="utf-8") if report.exists() else "No test report"
        print(text, flush=True)
        if result.returncode or "Finished testing" not in text or "FAIL!" in text:
            sys.exit(1)
