#!/usr/bin/env bash

# You must export CMAKE_PREFIX_PATH to find XcpNgGeneric like this:
# export CMAKE_PREFIX_PATH=/home/ronan/Projects/xcp-ng-generic-lib/build

mkdir -p build && cd build && \
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DENABLE_TOOLS=OFF -DENABLE_TESTS=OFF -DENABLE_OCAML_BINDING=YES && \
make
