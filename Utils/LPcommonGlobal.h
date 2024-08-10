#ifndef __LPCOMMONGLOBAL_H__
#define __LPCOMMONGLOBAL_H__
#include <QtGlobal>
#if QT_VERSION <= QT_VERSION_CHECK(6,0,0)
#include <QtCore/qglobal.h>
#endif
#if defined(LPCOMMON_LIBRARY)
#  define LPCOMMON_EXPORT Q_DECL_EXPORT
#else
#  define LPCOMMON_EXPORT Q_DECL_IMPORT
#endif
#endif
