#!/bin/bash
current_path=$(cd $(dirname $0); pwd)
parent_path=$(dirname ${current_path})
symbol_path=${parent_path}/lib/breakpad/symbols
bin_path=${parent_path}/lib/breakpad/bin

function FileSuffix() {
    local filename="$1"
    if [ -n "$filename" ]; then
        echo "${filename##*.}"
    fi
}

if [ -f ${bin_path}/dump_syms ]; then
    mkdir -p ${symbol_path}

    ${bin_path}/dump_syms ${parent_path}/build/$1/bin/QSmartAssistant > QSmartAssistant.sym
    hash=`head -n 1 QSmartAssistant.sym | awk '{print $4}'`
    mkdir -p ${symbol_path}/QSmartAssistant/${hash}
    mv QSmartAssistant.sym ${symbol_path}/QSmartAssistant/${hash}
    for path in ~/.config/QSmartAssistant/plugins/*
    do
        file=`basename ${path}`
        if [ "$(FileSuffix ${file})" = "so" ]; then
            ${bin_path}/dump_syms ${path} > ${file}.sym
            hash=`head -n 1 ${file}.sym | awk '{print $4}'`
            mkdir -p ${symbol_path}/${file}/${hash}
            mv ${file}.sym ${symbol_path}/${file}/${hash}
        fi
    done
fi