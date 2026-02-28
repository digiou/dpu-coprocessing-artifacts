#!/bin/bash

echo "3. Fetch binaries..."

# Fetch turbobench source
echo "Fetching turbobench source..."
cd ../experiments/local-compress/bin
git clone --depth=1 --recursive https://github.com/powturbo/TurboBench.git >/dev/null 2>&1
cp TurboBench/makefile TurboBench/makefile.orig
cp makefile_turbobench TurboBench/makefile  # use arm-friendly build file
cd TurboBench
make clean >/dev/null 2>&1
# make >/dev/null 2>&1
# mv makefile.orig makefile
cd ../../../..

# Fetch vcpkg
