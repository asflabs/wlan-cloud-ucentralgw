#!/bin/bash
mkdir -p /pvs_workdir/cmake-build
pushd /pvs_workdir/cmake-build
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=On /github/workspace
pwd
ll
pvs-studio-analyzer analyze -f compile_commands.json -o pvs.log -j8
plog-converter -a GA:1,2 -t tasklist -o "/github/workspace$1" pvs.log
popd