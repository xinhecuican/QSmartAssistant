#include "IPluginHelper.h"

// IPluginHelper 是 LPCOMMON_EXPORT 的抽象接口。在 Windows(MSVC) 下，
// __declspec(dllimport) 的类即使内联定义构造/析构，也会被当作“从 DLL 导入”，
// 因此必须在本库(lpcommon)内提供一份带 dllexport 的外联定义，否则链接
// QSmartAssistant.exe 时会报 LNK2019 未解析的外部符号。
// 在 Linux/GCC 下这同样安全（默认导出所有符号）。

IPluginHelper::IPluginHelper() {}

IPluginHelper::~IPluginHelper() {}
