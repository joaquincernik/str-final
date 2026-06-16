#ifndef LOGGING_H
#define LOGGING_H

#include <WiFi.h>

void logPrint(const String &msg);
void logPrintln(const String &msg);
void initTelnetServer();
void telnetTask(void *pvParams);

#endif
