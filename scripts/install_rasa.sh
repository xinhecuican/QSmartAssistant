#!/bin/bash
current_path=$(cd $(dirname $0); pwd)
parent_path=$(dirname ${current_path})
lib_path=${parent_path}/lib/rasa
bert_path=${parent_path}/Data/rasa/data/chinese_rbtl3

sudo apt install python3-virtualenv
virtualenv ${lib_path} -p python3.10
echo ${lib_path}/bin/activate
source ${lib_path}/bin/activate
pip install -U 'setuptools<70.4.0,>=70.3.0'
pip install rasa==3.6.19
pip install jieba
pip install torch
pip install transformers
pip install emoji==1.7
pip install recognizers-text-suite
mkdir -p ${bert_path}
cd ${bert_path}
# wget https://huggingface.co/google-bert/bert-base-chinese/resolve/main/config.json
# wget https://huggingface.co/google-bert/bert-base-chinese/resolve/main/pytorch_model.bin
# wget https://huggingface.co/google-bert/bert-base-chinese/resolve/main/tf_model.h5
# wget https://huggingface.co/google-bert/bert-base-chinese/resolve/main/tokenizer_config.json
# wget https://huggingface.co/google-bert/bert-base-chinese/resolve/main/vocab.txt
wget https://huggingface.co/hfl/rbtl3/resolve/main/pytorch_model.bin
wget https://huggingface.co/hfl/rbtl3/resolve/main/vocab.txt
wget https://huggingface.co/hfl/rbtl3/resolve/main/config.json
