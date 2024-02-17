import openwakeword
openwakeword.utils.download_models()
from openwakeword.model import Model
import sys
import numpy as np
from PyQt5.QtCore import QSharedMemory

args = sys.argv[1:]
model = args[0]
chunkSize = int(args[1]) / 2
modelName = args[2]
owwModel = Model(wakeword_models=[model])

# 创建共享内存对象并设置键
shared_memory = QSharedMemory("lowpower_openwakeup")

# 尝试连接到共享内存
if not shared_memory.attach():
    print("Failed to attach to shared memory:", shared_memory.errorString())
    exit(1)


# data_array = data_array.astype('<i2')
while True:
    command = input().strip("\n")
    if command == 'p':
        # 读取共享内存中的数据
        data = bytes(shared_memory.data())

        # 将数据转换为int16类型的ndarray
        data_array = np.frombuffer(data, dtype=np.int16)
        shared_memory.lock()
        prediction = owwModel.predict(data_array)
        shared_memory.unlock()
        scores = list(owwModel.prediction_buffer[modelName])
        print(scores[-1])
        sys.stdout.flush()
    if command == 'r':
        owwModel.reset()

shared_memory.detach()
