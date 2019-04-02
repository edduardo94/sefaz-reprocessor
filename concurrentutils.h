#ifndef CONCURRENTUTILS_H
#define CONCURRENTUTILS_H
#include <QThread>
#include <QtConcurrent/QtConcurrent>

#ifdef __GNUG__
#define GET_FUNC __FUNCTION__
#else
#define GET_FUNC __func__
#endif

#define RUN_IN_UI \
if (QThread::currentThread() != thread()){\
   QMetaObject::invokeMethod(this, GET_FUNC);\
   return;\
}

#define RUN_CONCURRENT(f) \
if (QThread::currentThread() == thread()) {\
    QtConcurrent::run(this, &f);\
    return;\
}


#endif // CONCURRENTUTILS_H
