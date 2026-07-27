#include "report.h"
#include "tests.h"

#include <QAtomicInt>
#include <QDeadlineTimer>
#include <QEventLoop>
#include <QFuture>
#include <QFutureWatcher>
#include <QMutex>
#include <QMutexLocker>
#include <QReadWriteLock>
#include <QRunnable>
#include <QSemaphore>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QWaitCondition>
#include <QtConcurrent>

#include <numeric>

namespace {

int square(int value)
{
    return value * value;
}

bool isEven(int value)
{
    return value % 2 == 0;
}

void accumulate(int &result, int value)
{
    result += value;
}

int slowSum(int a, int b)
{
    QThread::msleep(5);
    return a + b;
}

class Worker : public QObject
{
    Q_OBJECT

public:
    Q_INVOKABLE int doubled(int value)
    {
        m_threadId = QThread::currentThreadId();
        return value * 2;
    }

    Qt::HANDLE workerThreadId() const { return m_threadId; }

public slots:
    void process(int value) { emit processed(value + 1); }

signals:
    void processed(int value);

private:
    Qt::HANDLE m_threadId = nullptr;
};

class CountingTask : public QRunnable
{
public:
    explicit CountingTask(QAtomicInt *counter) : m_counter(counter) { setAutoDelete(true); }
    void run() override { m_counter->fetchAndAddOrdered(1); }

private:
    QAtomicInt *m_counter;
};

} // namespace

void testConcurrency()
{
    report::section(QStringLiteral("Threads, synchronisation and QtConcurrent"));

    report::info(QStringLiteral("ideal thread count"),
                 QString::number(QThread::idealThreadCount()));
    report::info(QStringLiteral("global thread pool max threads"),
                 QString::number(QThreadPool::globalInstance()->maxThreadCount()));
    report::check(QStringLiteral("thread support is available"),
                  QThread::idealThreadCount() > 0);

    // --- QThread with a worker object ---------------------------------------
    {
        QThread thread;
        Worker worker;
        worker.moveToThread(&thread);
        thread.start();
        report::check(QStringLiteral("QThread starts"), thread.isRunning());

        QEventLoop loop;
        int received = 0;
        QObject::connect(&worker, &Worker::processed, &loop, [&](int value) {
            received = value;
            loop.quit();
        });
        QTimer::singleShot(10000, &loop, &QEventLoop::quit);
        QMetaObject::invokeMethod(&worker, "process", Qt::QueuedConnection, Q_ARG(int, 41));
        loop.exec();
        report::check(QStringLiteral("cross-thread queued signal delivery"), received == 42,
                      QString::number(received));

        int blockingResult = 0;
        const bool invoked = QMetaObject::invokeMethod(&worker, "doubled",
                                                       Qt::BlockingQueuedConnection,
                                                       Q_RETURN_ARG(int, blockingResult),
                                                       Q_ARG(int, 21));
        report::check(QStringLiteral("BlockingQueuedConnection returns a value"),
                      invoked && blockingResult == 42, QString::number(blockingResult));
        report::check(QStringLiteral("worker actually ran on the secondary thread"),
                      worker.workerThreadId() != QThread::currentThreadId());

        thread.quit();
        report::check(QStringLiteral("QThread quits and joins"), thread.wait(10000));
    }

    // --- thread pool and QRunnable -----------------------------------------
    {
        QAtomicInt counter(0);
        QThreadPool pool;
        pool.setMaxThreadCount(4);
        for (int i = 0; i < 64; ++i)
            pool.start(new CountingTask(&counter));
        report::check(QStringLiteral("QThreadPool runs all QRunnables"),
                      pool.waitForDone(30000) && counter.loadRelaxed() == 64,
                      QStringLiteral("counter=%1").arg(counter.loadRelaxed()));
    }

    // --- synchronisation primitives -----------------------------------------
    {
        QMutex mutex;
        int shared = 0;
        {
            QMutexLocker locker(&mutex);
            shared = 1;
        }
        report::check(QStringLiteral("QMutex/QMutexLocker"),
                      mutex.tryLock() && shared == 1);
        mutex.unlock();

        QReadWriteLock rwLock;
        report::check(QStringLiteral("QReadWriteLock allows concurrent readers"),
                      rwLock.tryLockForRead() && rwLock.tryLockForRead());
        rwLock.unlock();
        rwLock.unlock();

        QSemaphore semaphore(2);
        semaphore.acquire(2);
        report::check(QStringLiteral("QSemaphore acquire/release"),
                      semaphore.available() == 0 && !semaphore.tryAcquire(1, 10));
        semaphore.release(2);
        report::check(QStringLiteral("QSemaphore restores its resources"),
                      semaphore.available() == 2);

        QAtomicInt atomic(0);
        atomic.fetchAndAddOrdered(5);
        report::check(QStringLiteral("QAtomicInt operations"),
                      atomic.loadAcquire() == 5 && atomic.testAndSetOrdered(5, 7)
                              && atomic.loadAcquire() == 7);

        QDeadlineTimer deadline(50);
        report::check(QStringLiteral("QDeadlineTimer is not expired immediately"),
                      !deadline.hasExpired() && deadline.remainingTime() > 0);
        QThread::msleep(70);
        report::check(QStringLiteral("QDeadlineTimer expires"), deadline.hasExpired());
    }

    // --- wait condition ------------------------------------------------------
    {
        QMutex mutex;
        QWaitCondition condition;
        bool ready = false;

        QThread *notifier = QThread::create([&] {
            QThread::msleep(20);
            QMutexLocker locker(&mutex);
            ready = true;
            condition.wakeAll();
        });
        notifier->start();

        QMutexLocker locker(&mutex);
        bool woken = true;
        while (!ready)
            woken = condition.wait(&mutex, QDeadlineTimer(5000));
        locker.unlock();
        notifier->wait(5000);
        delete notifier;
        report::check(QStringLiteral("QWaitCondition wake-up"), woken && ready);
    }

    // --- QtConcurrent --------------------------------------------------------
    {
        const QList<int> input = { 1, 2, 3, 4, 5, 6, 7, 8 };

        const QList<int> squares = QtConcurrent::blockingMapped(input, square);
        report::check(QStringLiteral("QtConcurrent::blockingMapped"),
                      squares.size() == input.size() && squares.last() == 64);

        const QList<int> evens = QtConcurrent::blockingFiltered(input, isEven);
        report::check(QStringLiteral("QtConcurrent::blockingFiltered"), evens.size() == 4);

        const int total = QtConcurrent::blockingMappedReduced(input, square, accumulate);
        report::check(QStringLiteral("QtConcurrent::blockingMappedReduced"), total == 204,
                      QString::number(total));

        QFuture<int> future = QtConcurrent::run(slowSum, 40, 2);
        report::check(QStringLiteral("QtConcurrent::run returns a QFuture"),
                      future.result() == 42);
        report::check(QStringLiteral("QFuture reports completion"),
                      future.isFinished() && !future.isCanceled());

        QFuture<int> chained = QtConcurrent::run(slowSum, 20, 1).then([](int value) {
            return value * 2;
        });
        report::check(QStringLiteral("QFuture::then continuation"), chained.result() == 42,
                      QString::number(chained.result()));

        // QFutureWatcher needs a running event loop.
        QEventLoop loop;
        QFutureWatcher<int> watcher;
        bool watcherFinished = false;
        QObject::connect(&watcher, &QFutureWatcherBase::finished, &loop, [&] {
            watcherFinished = true;
            loop.quit();
        });
        watcher.setFuture(QtConcurrent::run(slowSum, 1, 2));
        QTimer::singleShot(10000, &loop, &QEventLoop::quit);
        loop.exec();
        report::check(QStringLiteral("QFutureWatcher emits finished()"),
                      watcherFinished && watcher.result() == 3);
    }
}

#include "t_concurrency.moc"