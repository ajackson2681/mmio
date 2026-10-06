#!/usr/bin/bash

g++ Tests.cpp -o Tests
./Tests
RES=$?
rm Tests

exit $RES
