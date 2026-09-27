# -*- coding: utf-8 -*-
"""1.9.29 客户端：新增 act_pickExport 原生动作（选片导出目录选择）。

与 act_pickFolder 区分：后者会触发扫描流程，这里只回推目录路径，
供 index.html 选片模块的「导出整理」使用。
"""
import io

P = r'D:\Test\workbuddy\2026-09-18-09-00-13\client\lightalbum\app.py'

anchor = """    def act_pickFolder(self, p):
        d = QFileDialog.getExistingDirectory(
            self.win, '选择照片文件夹', self.win.last_dir or os.path.expanduser('~'))
        if d:
            self.win.last_dir = d
            self.emit('folderPicked', {'path': d})
"""
block = anchor + """
    def act_pickExport(self, p):
        \"\"\"选片导出目录选择（与 pickFolder 区分，避免触发扫描流程）。\"\"\"
        d = QFileDialog.getExistingDirectory(
            self.win, '选择选片导出目录',
            self.win.last_dir or os.path.expanduser('~'))
        if d:
            self.win.last_dir = d
            self.emit('exportPicked', {'path': d})
"""
with io.open(P, 'r', encoding='utf-8') as f:
    s = f.read()
assert s.count(anchor) == 1, 'act_pickFolder anchor 出现 %d 次' % s.count(anchor)
s = s.replace(anchor, block, 1)
with io.open(P, 'w', encoding='utf-8') as f:
    f.write(s)
print('patch_app_pickexport: DONE')
