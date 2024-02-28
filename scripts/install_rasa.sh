current_path=$(cd $(dirname $0); pwd)
parent_path=$(dirname ${current_path})
lib_path=${parent_path}/lib/rasa

python -m venv ${lib_path}
source ${lib_path}/bin/activate
pip install rasa
