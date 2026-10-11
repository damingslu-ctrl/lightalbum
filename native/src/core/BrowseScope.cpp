// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#include "BrowseScope.h"

namespace BrowseScope {

int nextVisible(const QVector<BrowseSlot>& seq, int cur, int delta, bool hidePrivate)
{
    if (delta == 0 || cur < 0 || cur >= seq.size()) return -1;
    for (int i = cur + delta; i >= 0 && i < seq.size(); i += delta) {
        if (hidePrivate && seq[i].priv) continue;
        return i;
    }
    return -1;
}

} // namespace BrowseScope
