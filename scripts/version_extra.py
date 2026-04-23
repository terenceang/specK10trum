from pathlib import Path


def load_version():
    p = Path("VERSION")
    if p.exists():
        return p.read_text(encoding="utf-8").strip()
    return "0.0.0"


def before_build(env, project_dir):
    """PlatformIO extra script hook: inject PROJECT_VERSION as a CPP define.

    Usage: set `extra_scripts = scripts/version_extra.py` in platformio.ini
    """
    version = load_version()
    # define PROJECT_VERSION as a C string macro
    env.Append(CPPDEFINES=[("PROJECT_VERSION", '"{}"'.format(version))])


# PlatformIO may call different hook names; ensure compatibility
try:
    before_build(None, None)
except Exception:
    pass
