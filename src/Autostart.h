#pragma once

#include <QString>

namespace Autostart {
QString desktopPath();
bool isEnabled();
void setEnabled(bool on);
void refresh();   // перезаписать Exec текущим путём бинарника
}
