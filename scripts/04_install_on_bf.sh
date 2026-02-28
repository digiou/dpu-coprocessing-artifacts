#!/bin/bash

# Remove old projects and update
echo "4. Copying project to bf from host..."
ssh bf-pcie "rm -rf dpu-coprocessing-artifacts"
ssh cloud-48 "rm -rf dpu-coprocessing-artifacts"
ssh cloud-48 "ssh bf-pcie 'rm -rf dpu-coprocessing-artifacts'"

cd ../..
rsync -raP dpu-coprocessing-artifacts bf-pcie:/home/ubuntu
rsync -raP dpu-coprocessing-artifacts cloud-48:/home/dimitrios
ssh cloud-48 "rsync -raP dpu-coprocessing-artifacts bf-pcie:/home/ubuntu"

# 1. Build turbobench on cloud-48 (bf2-host) and BF-2
echo "Building turbobench on cloud-48 (bf2-host)..."
ssh cloud-48 "cd dpu-coprocessing-artifacts/experiments/local-compress/bin/TurboBench && make clean && make"
echo "Building turbobench on bf2..."
ssh cloud-48 "ssh bf-pcie 'cd dpu-coprocessing-artifacts/experiments/local-compress/bin/TurboBench && make clean'"
ssh cloud-48 "ssh bf-pcie 'cd dpu-coprocessing-artifacts/experiments/local-compress/bin/TurboBench && cp makefile makefile.orig'"
ssh cloud-48 "ssh bf-pcie 'cd dpu-coprocessing-artifacts/experiments/local-compress/bin && cp makefile_turbobench TurboBench/makefile'"
ssh cloud-48 "ssh bf-pcie 'cd dpu-coprocessing-artifacts/experiments/local-compress/bin/TurboBench && make'"
ssh cloud-48 "ssh bf-pcie 'cd dpu-coprocessing-artifacts/experiments/local-compress/bin/TurboBench && mv makefile.orig makefile'"

# 2. Build turbobench on sr675-1-h100 (bf3-host) and BF-3
echo "Building turbobench on sr675-1-h100 (bf3-host)..."
cd dpu-coprocessing-artifacts/experiments/local-compress/bin/TurboBench && make clean && make
cd ../../../../..
echo "Building turbobench on bf3..."
ssh bf-pcie "cd dpu-coprocessing-artifacts/experiments/local-compress/bin/TurboBench && make clean"
ssh bf-pcie "cd dpu-coprocessing-artifacts/experiments/local-compress/bin/TurboBench && cp makefile makefile.orig"
ssh bf-pcie "cd dpu-coprocessing-artifacts/experiments/local-compress/bin && cp makefile_turbobench TurboBench/makefile"
ssh bf-pcie "cd dpu-coprocessing-artifacts/experiments/local-compress/bin/TurboBench && make"
ssh bf-pcie "cd dpu-coprocessing-artifacts/experiments/local-compress/bin/TurboBench && mv makefile.orig makefile"

# 3. TODO: build vcpkg co-processing
