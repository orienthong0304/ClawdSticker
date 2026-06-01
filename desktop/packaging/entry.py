"""PyInstaller entry shim — gives the bundler an explicit script entry point.

Equivalent to `python -m deskbuddy_app`, but as a plain script so PyInstaller's
Analysis has an unambiguous starting module. `pathex` in the .spec puts the
`deskbuddy_app` package on the import path.
"""

from deskbuddy_app.app import main

if __name__ == "__main__":
    main()
