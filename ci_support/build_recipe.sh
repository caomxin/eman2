#!/usr/bin/env bash

set -xe

MYDIR="$(cd "$(dirname "$0")"; pwd -P)"

bash "${MYDIR}/../tests/future_import_tests.sh"

if [ ! -z ${TRAVIS} ];then
    source ci_support/setup_conda.sh

    conda install conda-build=3 -c defaults --yes --quiet
fi

if [ ! -z ${CIRCLECI} ];then
    source ${HOME}/miniconda2/bin/activate root
fi

conda install future -c defaults --yes --quiet
python -m compileall -q .

export CPU_COUNT=2

if [ "$(uname -s)" == "Darwin" ];then
    export EMAN_TEST_SKIP=1
fi

conda info -a
conda list
conda render recipes/eman
conda build purge-all

conda build recipes/eman -c cryoem -c defaults -c conda-forge --quiet
