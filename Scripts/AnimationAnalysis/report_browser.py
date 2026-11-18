"""Open HTML reports in a browser even when .html is associated with an IDE."""
import os
from pathlib import Path
import shutil
import subprocess
import webbrowser


def windows_browser_candidates():
    import winreg

    browsers = [
        ('whale.exe', 'Naver/Naver Whale/Application/whale.exe'),
        ('msedge.exe', 'Microsoft/Edge/Application/msedge.exe'),
        ('chrome.exe', 'Google/Chrome/Application/chrome.exe'),
        ('firefox.exe', 'Mozilla Firefox/firefox.exe'),
    ]
    for executable, relative in browsers:
        for hive in (winreg.HKEY_CURRENT_USER, winreg.HKEY_LOCAL_MACHINE):
            try:
                key_path = 'SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\' + executable
                with winreg.OpenKey(hive, key_path) as key:
                    value, _ = winreg.QueryValueEx(key, None)
                    yield Path(os.path.expandvars(value.strip('"')))
            except OSError:
                pass
        on_path = shutil.which(executable)
        if on_path:
            yield Path(on_path)
        for variable in ('PROGRAMFILES', 'PROGRAMFILES(X86)', 'LOCALAPPDATA'):
            directory = os.environ.get(variable)
            if directory:
                yield Path(directory) / relative


def open_report(report):
    report = Path(report).resolve()
    if not report.is_file():
        raise FileNotFoundError(f'Report not found: {report}')
    uri = report.as_uri()
    if os.name == 'nt':
        for executable in windows_browser_candidates():
            if executable.is_file():
                subprocess.Popen([str(executable), uri], shell=False)
                return executable.name
        raise RuntimeError('No supported browser found. Open a browser and use Ctrl+O to select ' + str(report))
    if not webbrowser.open(uri):
        raise RuntimeError('Browser could not open ' + str(report))
    return 'default browser'
