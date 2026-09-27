# -*- coding: utf-8 -*-
"""光影集 · 客户端打包脚本（PyInstaller）。

用法：  venv 的 python build.py [--dir|--onefile] [--debug]
产物：  dist/<产品>-<版本>-win64/<产品>/<产品>.exe    （onedir，默认，启动快）
        dist/<产品>-<版本>-win64/<产品>.exe          （onefile，便于单文件分发）

正式化要点
----------
*   名称 / 版本 / 引擎命名从 ``lightalbum/version.py`` **单一事实源**读取，不硬编码；
*   ``--version-file`` 写入 Windows 版本信息：文件属性里能看到产品名、版本、
    版权与修图引擎名（Lumen）；
*   输出目录带版本号；若同名目录已存在，则退化为时间戳目录 —— 覆盖旧目录 = 删除，
    会触发沙箱的批量删除保护（实测踩过）；
*   每次构建用全新 workpath，同理只写不删；不要加 ``--clean``。
"""
import re
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEBUG = '--debug' in sys.argv
# 默认 onedir（启动不再每次解压数百 MB，快一个数量级）；显式 --onefile 才打包成单文件。
ONEDIR = '--onefile' not in sys.argv
CONSOLE = ['--console'] if DEBUG else ['--windowed']


def read_version():
    """从 version.py 解析常量（不 import，避免连带拉起包内重依赖）。"""
    txt = (HERE / 'lightalbum' / 'version.py').read_text(encoding='utf-8')

    def g(key, default=''):
        m = re.search(r"^%s\s*=\s*['\"]([^'\"]*)['\"]" % key, txt, re.M)
        return m.group(1) if m else default

    return {
        'APP_NAME': g('APP_NAME', '光影集'),
        'APP_NAME_EN': g('APP_NAME_EN', 'LightAlbum'),
        'APP_VERSION': g('APP_VERSION', '0.0.0'),
        'ORG_NAME': g('ORG_NAME', 'lightalbum'),
        'COPYRIGHT': g('COPYRIGHT', ''),
        'ENGINE_NAME': g('ENGINE_NAME', 'Lumen'),
        'ENGINE_VERSION': g('ENGINE_VERSION', '0.0.0'),
    }


def _ver4(v):
    ps = v.split('-')[0].split('.')
    ns = [int(x) if x.isdigit() else 0 for x in ps[:3]]
    while len(ns) < 3:
        ns.append(0)
    return ns + [0]


def write_version_file(v, path):
    """生成 PyInstaller 的 Windows 版本信息文件（VSVersionInfo）。"""
    fv = tuple(_ver4(v['APP_VERSION']))
    content = """VSVersionInfo(
  ffi=FixedFileInfo(filevers=%s, prodvers=%s, mask=0x3f, flags=0x0,
      OS=0x40004, fileType=0x1, subtype=0x0, date=(0, 0)),
  kids=[
    StringFileInfo([StringTable('040904B0', [
      StringStruct('CompanyName', '%s'),
      StringStruct('FileDescription', '%s · 智能修图客户端'),
      StringStruct('FileVersion', '%s'),
      StringStruct('InternalName', '%s'),
      StringStruct('LegalCopyright', '%s'),
      StringStruct('OriginalFilename', '%s.exe'),
      StringStruct('ProductName', '%s %s'),
      StringStruct('ProductVersion', '%s'),
      StringStruct('Comments', 'Retouch engine: %s %s')])]),
    VarFileInfo([VarStruct('Translation', [1033, 1200])])
  ]
)
""" % (fv, fv, v['APP_NAME'], v['APP_NAME'], v['APP_VERSION'], v['APP_NAME_EN'],
       v['COPYRIGHT'], v['APP_NAME_EN'], v['APP_NAME'], v['APP_NAME_EN'],
       v['APP_VERSION'], v['ENGINE_NAME'], v['ENGINE_VERSION'])
    path.write_text(content, encoding='utf-8')


def main():
    v = read_version()
    name = (v['APP_NAME_EN'] + 'Dbg') if DEBUG else v['APP_NAME_EN']
    stamp = time.strftime('%m%d%H%M%S')
    rel = '%s-%s-win64' % (v['APP_NAME_EN'], v['APP_VERSION'])
    distpath = HERE / 'dist' / rel
    if distpath.exists():              # 覆盖 = 删除 → 触发沙箱保护；退化时间戳目录
        distpath = HERE / 'dist' / (rel + '-' + stamp)
    workpath = HERE / 'build' / ('w' + stamp)
    vfile = HERE / 'build' / 'version_info.txt'
    write_version_file(v, vfile)

    cmd = [
        sys.executable, '-m', 'PyInstaller',
        '--noconfirm', '--onedir' if ONEDIR else '--onefile', *CONSOLE,
        '--name', name,
        '--version-file', str(vfile),
        '--workpath', str(workpath),
        '--distpath', str(distpath),
        '--icon', str(HERE / 'icon.ico'),
        '--add-data', '%s;resources' % (HERE / 'resources'),
        str(HERE / 'run.py'),
    ]
    print('光影集打包  %s %s  (%s)' % (v['APP_NAME'], v['APP_VERSION'],
                                       'onedir' if ONEDIR else 'onefile'))
    print('  引擎     %s %s' % (v['ENGINE_NAME'], v['ENGINE_VERSION']))
    print('  workpath %s' % workpath)
    print('  distpath %s' % distpath)
    print('$', ' '.join(str(c) for c in cmd))
    rc = subprocess.call(cmd)
    if rc == 0:
        rel_dir = (distpath / name) if ONEDIR else distpath
        print('BUILD_OK %s' % (rel_dir / (name + '.exe')))
        print('RELEASE_DIR %s' % rel_dir)
    raise SystemExit(rc)


if __name__ == '__main__':
    main()
