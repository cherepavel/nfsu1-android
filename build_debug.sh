#!/bin/sh

# debug build with params

./gradlew assembleDebug \
    -Pnfsu.registerBlock=true \
    -Pnfsu.executorTlsArg=true
