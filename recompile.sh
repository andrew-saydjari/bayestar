#!/bin/sh

# Profiling options
prof_input=${1:-0}
prof_gen="FALSE"
prof_use="FALSE"
if [[ ${prof_input} -eq 1 ]]; then
    prof_gen="TRUE"
    prof_use="FALSE"
elif [[ ${prof_input} -eq 2 ]]; then
    prof_gen="FALSE"
    prof_use="TRUE"
fi

echo "PROFILING_GEN=${prof_gen}"
echo "PROFILING_USE=${prof_use}"

# Recompile bayestar
    # -finline-functions \
    # -funroll-loops \
    # -floop-interchange \
    # -mavx \
    # -DBOOST_USE_STATIC_LIBS=ON \

cmake -S . -B . \
    -DCMAKE_BUILD_TYPE=RELEASE \
    -DPROFILING_GEN=${prof_gen} \
    -DPROFILING_USE=${prof_use} \
    .

make VERBOSE=1 -j
