import tempfile
from pathlib import Path
import unittest
from unittest.mock import patch

import report_browser


class ReportBrowserTests(unittest.TestCase):
    def test_windows_uses_browser_executable_and_file_uri(self):
        with tempfile.TemporaryDirectory(prefix='report preview ') as directory:
            report = Path(directory) / 'report.html'
            browser = Path(directory) / 'whale.exe'
            report.touch()
            browser.touch()
            with patch.object(report_browser, 'windows_browser_candidates', return_value=[browser]), \
                 patch.object(report_browser.subprocess, 'Popen') as launch, \
                 patch.object(report_browser.webbrowser, 'open') as association:
                self.assertEqual(report_browser.open_report(report), 'whale.exe')
                launch.assert_called_once_with([str(browser), report.resolve().as_uri()], shell=False)
                association.assert_not_called()

    def test_missing_browser_reports_manual_open_instructions(self):
        with tempfile.TemporaryDirectory() as directory:
            report = Path(directory) / 'report.html'
            report.touch()
            with patch.object(report_browser, 'windows_browser_candidates', return_value=[]), \
                 patch.object(report_browser.webbrowser, 'open') as association:
                with self.assertRaisesRegex(RuntimeError, 'Ctrl\\+O'):
                    report_browser.open_report(report)
                association.assert_not_called()

    def test_missing_report_does_not_launch(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(report_browser.subprocess, 'Popen') as launch:
                with self.assertRaises(FileNotFoundError):
                    report_browser.open_report(Path(directory) / 'missing.html')
                launch.assert_not_called()


if __name__ == '__main__':
    unittest.main()
