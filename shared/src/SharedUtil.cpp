//
//  SharedUtil.cpp
//  hifi
//
//  Created by Stephen Birarda on 2/22/13.
//
//

#include "SharedUtil.h"
#include <cstdlib>
#include <bitset>

double usecTimestamp(timeval *time) {
    return (time->tv_sec * 1000000.0 + time->tv_usec);
}

double usecTimestampNow() {
    timeval now;
    gettimeofday(&now, NULL);
    return (now.tv_sec * 1000000.0 + now.tv_usec);
}

float randFloat () {
    return (rand()%10000)/10000.f;
}

void outputBits(char byte) {
    printf("%d: ", byte);
    for (int i = 0; i < 8; i++) {
        printf("%d", byte >> (7 - i) & 1);
    }
    printf("\n");
}