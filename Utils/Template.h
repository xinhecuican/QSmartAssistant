#ifndef TEMPLATE_H
#define TEMPLATE_H

#define DECLARE_INSTANCE(Class) \
Q_DISABLE_COPY(Class) \
public: \
    static Class* instance() \
    { \
        static QMutex mutex; \
        static QScopedPointer<Class> inst; \
        if (Q_UNLIKELY(!inst)) { \
            mutex.lock(); \
            if (!inst) inst.reset(new Class); \
            mutex.unlock(); \
        } \
        return inst.data(); \
    }

#endif // TEMPLATE_H
