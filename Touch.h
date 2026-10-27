#pragma once

#include "TandoTypes.h"

bool recalibrateMpr121(uint32_t now);
void printMprDiagnostics();
void updateTouch(uint32_t now);
void setupTouch();
