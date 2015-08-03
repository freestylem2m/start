#!/bin/bash

BASE=${0%/*}
"${BASE}/build.sh" &&
valgrind --leak-check=full --show-leak-kinds=all "${BASE}/netmanage"
