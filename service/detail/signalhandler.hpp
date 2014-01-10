#ifndef shared_service_detail_signalhandler_hpp_included_
#define shared_service_detail_signalhandler_hpp_included_

#include <memory>

#include <boost/noncopyable.hpp>
#include <boost/asio.hpp>

#include <boost/interprocess/anonymous_shared_memory.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/make_shared.hpp>

#include <boost/filesystem/path.hpp>

#include "dbglog/dbglog.hpp"
#include "utility/atfork.hpp"

#include "../service.hpp"

namespace bi = boost::interprocess;
namespace asio = boost::asio;
namespace fs = boost::filesystem;

namespace local = boost::asio::local;

namespace service { namespace detail {

namespace lib = std;
namespace placeholders = std::placeholders;

class Allocator : boost::noncopyable {
public:
    Allocator(std::size_t size)
        : mem_(bi::anonymous_shared_memory(size))
        , size_(size), offset_()
    {}

    template <typename T>
    T* get(std::size_t count = 1) {
        // TODO: check size
        auto data(static_cast<char*>(mem_.get_address()) + offset_);
        offset_ += sizeof(T) * count;
        return reinterpret_cast<T*>(data);
    }

private:
    bi::mapped_region mem_;
    std::size_t size_;
    std::size_t offset_;
};

class Terminator : boost::noncopyable {
public:
    Terminator(Allocator &mem, std::size_t size)
        : lock_(*new (mem.get<LockType>()) LockType())
        , pids_(mem.get< ::pid_t>(size)), size_(size)
    {
        // reset all slots
        for (auto &p : *this) { p = 0; }
    }

    bool add(::pid_t pid) {
        ScopedLock guard(lock_);
        if (!pid) { pid = ::getpid(); }
        if (find(pid)) { return true; }
        for (auto &p : *this) {
            if (!p) { p = pid; return true; }
        }
        // cannot add
        return false;
    }

    void remove(::pid_t pid) {
        ScopedLock guard(lock_);
        if (!pid) { pid = ::getpid(); }
        auto p(find(pid));
        if (p) { p = 0; }
    }

    bool find() {
        ScopedLock guard(lock_);
        return find(::getpid());
    }

    ::pid_t* begin() { return pids_; }
    ::pid_t* end() { return pids_ + size_; }

private:
    ::pid_t* find(::pid_t pid) {
        for (auto &p : *this) {
            if (p == pid) { return &p; }
        }
        return nullptr;
    }

    typedef bi::interprocess_mutex LockType;
    typedef bi::scoped_lock<LockType> ScopedLock;
    LockType &lock_;

    ::pid_t *pids_;
    std::size_t size_;
};

class CtrlConnection;

class SignalHandler : boost::noncopyable
{
public:
    typedef std::shared_ptr<SignalHandler> pointer;

    struct ScopedHandler {
        ScopedHandler(SignalHandler &h) : h(h) { h.start(); }
        ~ScopedHandler() { h.stop(); }

        SignalHandler &h;
    };

    SignalHandler(dbglog::module &log, Service &owner, pid_t mainPid
                  , const boost::optional<fs::path> &ctrlPath);

    ~SignalHandler();

    void terminate();

    /** Processes events and returns whether we should terminate.
     */
    bool process();

    void globalTerminate(bool value, ::pid_t pid);

    void logRotate();

private:
    void start();

    void stop();

    void startSignals();

    void signal(const boost::system::error_code &e, int signo);

    void markTerminated();

    void startAccept();

    void stopAccept();

    void newCtrlConnection(const boost::system::error_code &e
                           , lib::shared_ptr<CtrlConnection> con);

    void atFork(utility::AtFork::Event event);

    asio::io_service ios_;
    asio::signal_set signals_;
    Allocator mem_;
    Terminator terminator_;
    std::atomic_bool &terminated_;
    std::atomic_bool thisTerminated_;
    std::atomic<std::uint64_t> &logRotateEvent_;
    std::uint64_t lastLogRotateEvent_;
    std::atomic<std::uint64_t> &statEvent_;
    std::uint64_t lastStatEvent_;
    dbglog::module &log_;
    Service &owner_;
    pid_t mainPid_;

    // control support
    boost::optional<fs::path> ctrlPath_;
    local::stream_protocol::acceptor ctrl_;
};

} } // namespace service::detail

#endif // shared_service_detail_signalhandler_hpp_included_
