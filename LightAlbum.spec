# -*- mode: python ; coding: utf-8 -*-


a = Analysis(
    ['D:/Test/workbuddy/2026-09-18-09-00-13/client/run.py'],
    pathex=[],
    binaries=[],
    datas=[('D:/Test/workbuddy/2026-09-18-09-00-13/client/resources', 'resources')],
    hiddenimports=[],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name='LightAlbum',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    version='D:/Test/workbuddy/2026-09-18-09-00-13/client/build/version_info.txt',
    icon=['D:/Test/workbuddy/2026-09-18-09-00-13/client/icon.ico'],
)
coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=True,
    upx_exclude=[],
    name='LightAlbum',
)
