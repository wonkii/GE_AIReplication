"""Rebuild a saved report using standard Python; optionally open it directly in a browser."""
import argparse
from pathlib import Path
from reaction_warp import render

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('directory', type=Path)
parser.add_argument('--open', action='store_true')
args = parser.parse_args()
if not (args.directory / 'README.md').is_file():
    parser.error('Select an existing Reaction Warp task directory containing README.md')
render(args.directory)
if args.open:
    from report_browser import open_report
    open_report(args.directory / 'report.html')
print(args.directory / 'report.html')
