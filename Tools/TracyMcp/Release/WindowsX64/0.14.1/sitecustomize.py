import site
from pathlib import Path

site.addsitedir(str(Path(__file__).resolve().parent / "PythonPackages"))