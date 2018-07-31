#ifndef OTAHANDLER_H
#define OTAHANDLER_H

typedef enum {
    OTAMODE_HOT,
    OTAMODE_TEST
} otaMode_t;

void startOTA(char *Servername, uint16_t Port, char* Filename, otaMode_t otamode);

#endif