#pragma once
#include "uvc_discovery.h"
#include <optional>
class QWidget;
std::optional<UvcPair> chooseUvcPair(QWidget *parent,bool autoAcceptSingle=false);
