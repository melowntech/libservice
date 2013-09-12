#include <cstdint>
#include <memory>
#include <atomic>
#include <system_error>
#include <signal.h>
#include <string.h>

#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>

#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <boost/optional.hpp>
#include <boost/utility/in_place_factory.hpp>

#include <boost/interprocess/anonymous_shared_memory.hpp>

#include "service.hpp"
#include "pidfile.hpp"

namespace asio = boost::asio;
namespace fs = boost::filesystem;
namespace bi = boost::interprocess;

namespace service {

namespace {
    inline void* getAddress(bi::mapped_region &mem, std::size_t offset = 0)
    {
        return static_cast<char*>(mem.get_address()) + offset;
    }

    class Allocator : boost::noncopyable {
    public:
        Allocator(std::size_t size)
            : mem_(bi::anonymous_shared_memory(1024))
            , size_(size), offset_()
        {}

        template <typename T>
        T* get(std::size_t count = 1)
        {
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
            : pids_(mem.get< ::pid_t>(size)), size_(size)
        {
            // reset all slots
            for (auto &p : *this) { p = 0; }
        }

        bool add() {
            auto pid(::getpid());
            if (find(pid)) { return true; }
            for (auto &p : *this) {
                if (!p) { p = pid; return true; }
            }
            // cannot add
            return false;
        }

        void remove() {
            auto p(find(::getpid()));
            if (p) { p = 0; }
        }

        bool find() {
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

        ::pid_t *pids_;
        std::size_t size_;
    };
}

struct service::SignalHandler : boost::noncopyable {
public:
    SignalHandler(dbglog::module &log, service &owner, pid_t mainPid)
        : signals_(ios_, SIGTERM, SIGINT, SIGHUP)
        , mem_(4096)
        , terminator_(mem_, 32)
        , terminated_(* new (mem_.get<std::atomic_bool>())
                      std::atomic_bool(false))
        , thisTerminated_(false)
          // TODO: what about alignment?
        , logRotateEvent_(* new (mem_.get<std::atomic<std::uint64_t> >())
                          std::atomic<std::uint64_t>(0))
        , lastLogRotateEvent_(0)
        , statEvent_(* new (mem_.get<std::atomic<std::uint64_t> >())
                     std::atomic<std::uint64_t>(0))
        , lastStatEvent_(0)
        , log_(log), owner_(owner), mainPid_(mainPid)
    {
        signals_.add(SIGUSR1);
    }

    struct ScopedHandler {
        ScopedHandler(SignalHandler &h) : h(h) { h.start(); }
        ~ScopedHandler() { h.stop(); }

        SignalHandler &h;
    };

    void terminate() { terminated_ = true; }

    /** Processes events and returns whether we should terminate.
     */
    bool process() {
        ios_.poll();

        // TODO: last event should be (process-local) atomic to handle multiple
        // threads calling this function

        // check for logrotate request
        {
            auto value(logRotateEvent_.load());
            if (value != lastLogRotateEvent_) {
                LOG(info3, log_) << "Logrotate: <" << owner_.logFile() << ">.";
                dbglog::log_file(owner_.logFile().string());
                LOG(info4, log_)
                    << "Service " << owner_.name << '-' << owner_.version
                    << ": log rotated.";
                lastLogRotateEvent_ = value;
            }
        }

        // check for statistics request
        {
            auto value(statEvent_.load());
            if ((value != lastStatEvent_) && (::getpid() == mainPid_)) {
                // statistics are processed only in main process
                owner_.stat();
                lastStatEvent_ = value;
            }
        }

        return terminated_ || thisTerminated_;
    }

    void globalTerminate(bool value) {
        if (value) {
            terminator_.add();
        } else {
            terminator_.remove();
        }
    }

private:
    void start() {
        signals_.async_wait(boost::bind(&SignalHandler::signal, this
                                        , asio::placeholders::error
                                        , asio::placeholders::signal_number));
    }

    void stop() {
        signals_.cancel();
    }

    void signal(const boost::system::error_code &e, int signo) {
        if (e) {
            if (boost::asio::error::operation_aborted == e) {
                return;
            }
            start();
        }

        // ignore signal '0'
        if (!signo) {
            start();
            return;
        }

        auto signame(::strsignal(signo));

        LOG(debug, log_)
            << "SignalHandler received signal: <" << signo
            << ", " << signame << ">.";
        switch (signo) {
        case SIGTERM:
        case SIGINT:
            LOG(info2, log_)
                << "Terminate signal: <" << signo << ", " << signame << ">.";
            markTerminated();
            break;

        case SIGHUP:
            // mark logrotate
            ++logRotateEvent_;
            break;

        case SIGUSR1:
            // mark statistics request
            ++statEvent_;
            break;
        }
        start();
    }

    void markTerminated() {
        thisTerminated_ = true;

        // check for global termination
        if (terminator_.find()) {
            LOG(info1) << "Global terminate.";
            terminated_ = true;
        } else {
            LOG(info1) << "Local terminate.";
        }
    }

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
    service &owner_;
    pid_t mainPid_;
};

service::service(const std::string &name, const std::string &version
                 , int flags)
    : program(name, version, flags)
{}

service::~service()
{}

namespace {

void switchPersona(dbglog::module &log
                   , const std::string &username
                   , const std::string &groupname)
{
    bool switchUid(false);
    bool switchGid(false);
    uid_t uid(-1);
    gid_t gid(-1);

    if (username.empty() && groupname.empty()) { return; }
    LOG(info3, log)
        << "Trying to run under " << username << ":" << groupname << ".";
    if (!username.empty()) {
        auto pwd(::getpwnam(username.c_str()));
        if (!pwd) {
            LOGTHROW(err3, std::runtime_error)
                << "There is no user <" << username
                << "> present on the system.";
            throw;
        }

        // get uid and gid
        uid = pwd->pw_uid;
        gid = pwd->pw_gid;
        switchUid = switchGid = true;
    }

    if (!groupname.empty()) {
        auto gr(::getgrnam(groupname.c_str()));
        if (!gr) {
            LOGTHROW(err3, std::runtime_error)
                << "There is no group <" << groupname
                << "> present on the system.";
            throw;
        }
        gid = gr->gr_gid;
        switchGid = true;
    }

    // change log file owner to uid/gid before persona change
    dbglog::log_file_owner(uid, gid);

    // TODO: check whether we do not run under root!

    if (switchGid) {
        LOG(info3, log) << "Switching to gid <" << gid << ">.";
        if (-1 == ::setgid(gid)) {
            std::system_error e(errno, std::system_category());
            LOG(fatal, log)
                << "Cannot switch to gid <" << gid << ">: "
                << "<" << e.code() << ", " << e.what() << ">.";
            throw e;
        }
    }

    if (switchUid) {
        LOG(info3, log)
            << "Setting supplementary groups for user <"
            << username << ">.";
        if (-1 == ::initgroups(username.c_str(), gid)) {
            std::system_error e(errno, std::system_category());
            LOG(fatal, log)
                << "Cannot initialize supplementary groups for user <"
                << username << ">: <" << e.code()
                << ", " << e.what() << ">.";
            throw e;
        }

        LOG(info3, log) << "Switching to uid <" << uid << ">.";
        if (-1 == ::setuid(uid)) {
            std::system_error e(errno, std::system_category());
            LOG(fatal, log)
                << "Cannot switch to uid <" << uid << ">: "
                << "<" << e.code() << ", " << e.what() << ">.";
            throw e;
        }
    }

    LOG(info3, log)
        << "Run under " << username << ":" << groupname << ".";
}

int sendSignal(dbglog::module &log, const fs::path &pidFile
               , const std::string &signal)
{
    int signo = 0;
    if (signal == "stop") {
        signo = SIGTERM;
    } else if (signal == "logrotate") {
        signo = SIGHUP;
    } else if (signal == "test") {
        signo = 0;
    } else if (signal == "stat") {
        signo = SIGUSR1;
    } else {
        LOG(fatal, log) << "Unrecognized signal: <" << signal << ">.";
        return EXIT_FAILURE;
    }

    LOG(info1)
        << "About to send signal <" << signal << "> to running process.";
    try {
        if (!pidfile::signal(pidFile, signo)) {
            // not running -> return 1
            return 1;
        }
    } catch (const std::exception &e) {
        LOG(fatal, log)
            << "Cannot signal running instance: <" << e.what() << ">.";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

bool waitForChildInitialization(dbglog::module &log, utility::Runnable &master
                                , int fd)
{
    char buffer[1024];
    for (;;) {
        auto r(::read(fd, buffer, sizeof(buffer)));
        if (-1 == r) {
            if (EINTR == errno) {
                if (!master.isRunning()) {
                    LOG(warn4, log)
                        << "Terminated during daemonization.";
                    return false;
                }
            } else {
                LOG(fatal, log)
                    << "Failed to read pid from notifier pipe: "
                    << errno;
                return false;
            }
        }

        if (!r) {
            // process terminated, try to wait for it
            // TODO: implement
            return true;
        }
    }
}

bool daemonizeNochdir(false);
bool daemonizeNoclose(false);
int notifierFd(-1);

void daemonizeFinish()
{
    // replace stdin/out/err with /dev/null
    if (!daemonizeNoclose) {
        // TODO: check errors
        auto null(::open("/dev/null", O_RDWR));
        ::dup2(null, STDIN_FILENO);

        // tie STDOUT_FILENO and STDERR_FILENO to log instead!
        // ::dup2(null, STDOUT_FILENO);
        // ::dup2(null, STDERR_FILENO);
        dbglog::tie(STDOUT_FILENO);
        dbglog::tie(STDERR_FILENO);
    }

    // disable log here
    dbglog::log_console(false);

    // OK, signal we are ready to serve
    if (notifierFd >= 0) {
        // TODO: check errors
        ::close(notifierFd);
    }

    if (-1 == ::pthread_atfork(nullptr, nullptr, nullptr)) {
        LOG(warn4) << "Atfork deregistration failed: " << errno;
    }
}

extern "C" {
    void atfork(void) {
        daemonizeFinish();
    }
}

} // namespace

void service::preConfigHook(const po::variables_map &vars)
{
    if (!vars.count("signal")) {
        // normal startup
        if (vars.count("pidfile")) {
            // OK, sanity check
            auto pid(pidfile::signal(vars["pidfile"].as<fs::path>(), 0));
            if (pid) {
                LOG(fatal, log_)
                    << "Service " << identity()
                    << " is already running with pid <" << pid << ">.";
                throw immediate_exit(EXIT_FAILURE);
            }
        }
        return;
    }


    // just send signal to running instance
    if (!vars.count("pidfile")) {
        LOG(fatal, log_) << "Pid file must be specified to send signal.";
        throw immediate_exit(EXIT_FAILURE);
    }

    // send signal and terminate
    immediateExit(sendSignal(log_, vars["pidfile"].as<fs::path>()
                             , vars["signal"].as<std::string>()));
}

int service::operator()(int argc, char *argv[])
{
    dbglog::thread_id("main");

    bool daemonize(false);

    std::string username;
    std::string groupname;
    fs::path pidFilePath;

    try {
        po::options_description genericCmdline("command line options");

        po::options_description genericConfig
            ("configuration file options (all options can be overridden "
             "on command line)");

        genericCmdline.add_options()
            ("daemonize,d"
             , "Run in daemon mode (otherwise run in foreground).")
            ("daemonize-nochdir"
             , "Do not leave current directory after forking to background.")
            ("daemonize-noclose"
             , "Do not close STDIN/OUT/ERR after forking to background.")
            ("pidfile", po::value(&pidFilePath)
             , "Path to pid file.")
            ("signal,s", po::value<std::string>()
             , "Signal to be sent to running instance: stop, logrotate, test.")
            ;

        genericConfig.add_options()
            ("service.user", po::value(&username)
             , "Switch process persona to given username.")
            ("service.group", po::value(&groupname)
             , "Switch process persona to given group name.")
            ;

        auto vm(program::configure(argc, argv, genericCmdline, genericConfig));
        daemonize = vm.count("daemonize");
        daemonizeNochdir = vm.count("daemonize-nochdir");
        daemonizeNoclose = vm.count("daemonize-noclose");

        if (!daemonize && (daemonizeNochdir || daemonizeNoclose)) {
            LOG(warn4, log_)
                << "Options --daemonize-nochdir and --daemonize-noclose "
                "make sense only together with --daemonize.";
        }

        // make sure pidfile is an absolute path
        if (!pidFilePath.empty()) {
            pidFilePath = absolute(pidFilePath);
        }
    } catch (const immediate_exit &e) {
        return e.code;
    }

    LOG(info4, log_) << "Service " << identity() << " starting.";

    // daemonize if asked to do so

    // daemonization code
    // NB: this piece of code must be terminated only by "_exit"
    // returning or calling exit() calls destructors and bad things happen!
    if (daemonize) {
        LOG(info4, log_) << "Forking to background.";

        // go away!
        if (!daemonizeNochdir) {
            if (-1 == ::chdir("/")) {
                std::system_error e(errno, std::system_category());
                LOG(warn3, log_)
                    << "Cannot cd to /: "
                    << "<" << e.code() << ", " << e.what() << ">.";
            }
        }

        int notifier1[2];
        if (-1 == ::pipe(notifier1)) {
            LOG(fatal, log_) << "Failed to create notifier pipe: " << errno;
            _exit(EXIT_FAILURE);
        }

        int notifier2[2];
        if (-1 == ::pipe(notifier2)) {
            LOG(fatal, log_) << "Failed to create notifier pipe: "
                             << errno;
            _exit(EXIT_FAILURE);
        }

        auto pid(::fork());
        if (-1 == pid) {
            LOG(fatal, log_) << "Failed to fork: " << errno;
            return EXIT_FAILURE;
        }

        if (pid) {
            // starter process; close both notifiers
            ::close(notifier1[1]);
            ::close(notifier2[0]);
            ::close(notifier2[1]);

            // parent -> starting process
            if (!waitForChildInitialization(log_, *this, notifier1[0])) {
                LOG(fatal, log_) << "Child process failed.";
                _exit(EXIT_FAILURE);
            }

            LOG(info4, log_)
                << "Service " << identity() << " running at background.";
            _exit(EXIT_SUCCESS);
        } else {
            // intermediate process

            // child, set sid and for again
            if (-1 == ::setsid()) {
                LOG(fatal, log_) << "Unable to become a session leader: "
                                 << errno;
                _exit(EXIT_FAILURE);
            }

            pid = ::fork();
            if (-1 == pid) {
                LOG(fatal, log_) << "Failed secondary fork: " << errno;
                _exit(EXIT_FAILURE);
            }

            if (pid) {
                // OK, intermediate process; close second notifier
                ::close(notifier2[1]);

                if (!waitForChildInitialization(log_, *this, notifier2[0])) {
                    LOG(fatal, log_) << "Child process failed.";
                    _exit(EXIT_FAILURE);
                }
                _exit(EXIT_SUCCESS);
            }

            // close intermediate process notifier
            ::close(notifier1[0]);
            ::close(notifier1[1]);
            ::close(notifier2[0]);

            // we are in the daemonized process
            notifierFd = notifier2[1];

            // we want to close notifier when something forks
            if (-1 == ::pthread_atfork(nullptr, nullptr, &atfork)) {
                LOG(fatal, log_)
                    << "Atfork registration failed: " << errno;
                _exit(EXIT_FAILURE);
            }
        }

        LOG(info4, log_) << "Running in background.";
    }

    if (!pidFilePath.empty()) {
        // handle pidfile
        try {
            pidfile::allocate(pidFilePath);
        } catch (const std::exception &e) {
            LOG(fatal, log_) << "Cannot allocate pid file: " << e.what();
            return EXIT_FAILURE;
        }
    }

    prePersonaSwitch();
    try {
        switchPersona(log_, username, groupname);
    } catch (const std::exception &e) {
        return EXIT_FAILURE;
    }
    postPersonaSwitch();

    // start signal handler in main process
    signalHandler_.reset(new SignalHandler(log_, *this, ::getpid()));

    // we are the one that terminates whole daemon!
    globalTerminate(true);

    int code = EXIT_SUCCESS;
    {

        SignalHandler::ScopedHandler signals(*signalHandler_);

        Cleanup cleanup;
        try {
            cleanup = start();
        } catch (const immediate_exit &e) {
            if (daemonize) {
                LOG(fatal, log_)
                    << "Startup exits with exit status: " << e.code << ".";
            }
            return e.code;
        }

        if (!isRunning()) {
            LOG(info4, log_) << "Terminated during startup.";
            return EXIT_FAILURE;
        }

        if (daemonize) {
            daemonizeFinish();
        }

        code = run();
    }

    if (code) {
        LOG(err4, log_) << "Terminated with error " << code << '.';
    } else {
        LOG(info4, log_) << "Terminated normally.";
    }

    return code;
}

void service::stop()
{
    signalHandler_->terminate();
}

bool service::isRunning() {
    return !signalHandler_->process();
}

void service::globalTerminate(bool value)
{
    signalHandler_->globalTerminate(value);
}

void service::configure(const std::vector<std::string> &)
{
    throw po::error
        ("Program asked to collect unrecognized options "
         "although it is not processing them. Go fix your program.");
}

inline bool service::help(std::ostream &, const std::string &)
{
    return false;
}

} // namespace service
