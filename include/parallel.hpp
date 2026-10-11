#pragma once

#include <QList>
#include <QThread>
#include <QtConcurrent>
#include <algorithm>
#include <utility>

// Helpers that split [0, n) into contiguous chunks and run them on Qt's
// global thread pool. Both block until all chunks are done.
namespace parallel
{
using Range = std::pair<long, long>;

inline QList<Range>
chunks(long n)
{
    constexpr long kMinChunk = 1L << 16; // not worth a thread below this
    const long count
        = std::clamp<long>(n / kMinChunk, 1, QThread::idealThreadCount());
    QList<Range> ranges;
    for (long i = 0; i < count; ++i)
        ranges.append({n * i / count, n * (i + 1) / count});
    return ranges;
}

// fn(begin, end) for each chunk
template <class Fn>
void
forRange(long n, Fn fn)
{
    QtConcurrent::blockingMap(chunks(n), [fn](Range &r) { fn(r.first, r.second); });
}

// part = fn(begin, end) per chunk; merge(total, part) combines them
// (in no particular order)
template <class Result, class Fn, class Merge>
Result
reduceRange(long n, Fn fn, Merge merge)
{
    return QtConcurrent::blockingMappedReduced<Result>(
        chunks(n), [fn](const Range &r) { return fn(r.first, r.second); },
        [merge](Result &total, const Result &part) { merge(total, part); });
}
} // namespace parallel
