#ifndef __LPCOMMONGLOBAL_H__
#define __LPCOMMONGLOBAL_H__
#include <QtCore/qglobal.h>
#if defined(LPCOMMON_LIBRARY)
#  define LPCOMMON_EXPORT Q_DECL_EXPORT
#else
#  define LPCOMMON_EXPORT Q_DECL_IMPORT
#endif
#endif