#pragma once

#include <QImage>

#include "backend/app/BackgroundFrame.h"

namespace frontend::qt {

QImage toQImage(const backend::BackgroundFrame &frame);

} // namespace frontend::qt
