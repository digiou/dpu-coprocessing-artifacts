# dpu-coprocessing-artifacts
Artifacts for our **Analyzing Near-Network Hardware Acceleration with Co-Processing on DPUs** paper.

The repository contains one sub-directory:

- `experiments`: multiple sub-experiments and related common files with their own READMEs

## Experiment Descriptions
- `capability-check`: DOCA implementation of a simple check for hardware offloading of tasks related to paper, helper
- `co-processing`: CPU and DOCA implementation of joined threads performing the same task with various levels of co-processing
- `local-compress`: CPU and DOCA implementation of DEFLATE, INFLATE, and LZ4 tasks
- `regex`: CPU and DOCA implementation of Regex tasks, additionally with `re2`
- `sbc-bench`: results from running [sbc-bench](https://github.com/ThomasKaiser/sbc-bench) on the ARM CPUs, helper

## Dependencies
- NVIDIA DOCA v2.9.0 from [here](https://docs.nvidia.com/doca/archive/2-9-0/index.html)
- `vcpkg` for co-processing, local-compress, and regex, from [here](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started?pivots=shell-bash#1---set-up-vcpkg)
- Python dependencies: in per-experiment `requirements.txt`
- Turbobench for CPU-related (de)compression tasks from [here](https://github.com/powturbo/TurboBench)
- `meson` and `ninja` (from [here](https://packages.ubuntu.com/jammy/meson) and [here](https://packages.ubuntu.com/jammy/ninja-build))

## Generating plots
Each individual experiment has its own Jupyter notebook that produce the related figures. We will update this process to automate it further.


## Experiment data
Currently, the experiments make use of two datasets, namely Silesia for (de)compression experiments
and US Accidents (updated) for regex-related experiments. Data are available in two ways:
- `git submodule` for [Silesia corpus](https://sun.aei.polsl.pl//~sdeor/index.php?page=silesia)
    - run `git submodule update --init --recursive`
- direct download for the [US Accidents dataset](https://smoosavi.org/datasets/us_accidents)
    - updated version is [here](https://www.kaggle.com/datasets/sobhanmoosavi/us-accidents)