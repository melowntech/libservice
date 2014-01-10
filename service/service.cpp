#include <cstdint>
#include <memory>
#include <atomic>
#include <system_error>
#include <tuple>
#include <cstring>

#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>

#include <boost/filesystem.hpp>
#include <boost/optional.hpp>
#include <boost/utility/in_place_factory.hpp>

#include "service.hpp"
#include "pidfile.hpp"
#include "detail/signalhandler.hpp"

#include "utility/steady-clock.hpp"
#include "utility/path.hpp"

namespace fs = boost::filesystem;

namespace service {

Service::Service(const std::string &name, const std::string &version
                 , int flags)
    : Program(name, version, flags)
{}

Service::~Service()
{}

namespace {

void switchPersona(dbglog::module &log, const Service::Config &config)
{
    const auto &username(config.username);
    const auto &groupname(config.groupname);

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

struct SigDef {
    std::string signal;
    int signo;
    std::time_t timeout;

    SigDef() : signo(), timeout(-1) {}
};

namespace Signal { enum {
    stop = SIGTERM
    , logrotate = SIGHUP
    , stat = SIGUSR1
    , status = 0
}; }

SigDef parseSigDef(dbglog::module &log, const std::string &sigDef)
{
    auto slash(sigDef.find('/'));

    SigDef def;
    def.signal = sigDef.substr(0, slash);

    auto &signal(def.signal);
    auto &signo(def.signo);
    if (signal == "stop") {
        signo = Signal::stop;
    } else if (signal == "logrotate") {
        signo = Signal::logrotate;
    } else if (signal == "status") {
        signo = Signal::status;
    } else if (signal == "stat") {
        signo = Signal::stat;
    } else {
        LOG(fatal, log) << "Unrecognized signal: <" << signal << ">.";
        service::immediate_exit(3);
    }

    if (slash != std::string::npos) {
        if (signo == Signal::stop) {
            try {
                def.timeout = boost::lexical_cast<std::time_t>
                    (sigDef.substr(slash + 1));
            } catch (const boost::bad_lexical_cast&) {
                LOG(fatal, log)
                    << "Invalid timeout specification ("
                    << sigDef.substr(slash + 1) << ").";
                service::immediate_exit(3);
            }
        } else {
            LOG(warn2) << "Ignoring timeout specification for Signal <"
                       << signal << ">.";
        }
    }

    return def;
}

int waitForStop(dbglog::module &log, const fs::path &pidFile
                , const SigDef &def)
{
    try {
        utility::steady_clock::time_point
            deadline(utility::steady_clock::now()
                     + std::chrono::seconds(def.timeout));
        for (bool first(true);; first = false) {
            if (!pidfile::signal(pidFile, def.signo)) {
                // fail if process is not running during first test
                // OK if process was running but finished now
                return first ? 1 : 0;
            }

            if (utility::steady_clock::now() >= deadline) {
                // program was running but cannot stop in given time
                return 2;
            }

            // sleep a bit and retry
            ::usleep(100000);
        }
    } catch (const std::exception &e) {
        LOG(fatal, log)
            << "Cannot signal running instance: <" << e.what() << ">.";
        return 3;
    }
}

int processStatus(dbglog::module &log, const fs::path &pidFile
                  , const SigDef &def)
{
    try {
        auto pid(pidfile::signal(pidFile, def.signo, true));
        if (!pid) {
            // Program is not running and the pid file exists.
            return 1;
        } else if (pid < 0) {
            // Program is not running.
            return 3;
        }
        // program is running
        return 0;
    } catch (const std::exception &e) {
        LOG(fatal, log)
            << "Cannot signal running instance: <" << e.what() << ">.";
        // Unable to determine program status.
        return 4;
    }
}

int sendSignal(dbglog::module &log, const fs::path &pidFile
               , const std::string &arg)
{
    auto def(parseSigDef(log, arg));

    LOG(info1, log)
        << "About to send signal <" << def.signal << "> to running process.";

    // waiting stop has special handler
    switch (def.signo) {
    case Signal::stop:
        if (def.timeout >= 0) {
            return waitForStop(log, pidFile, def);
        }
        break;

    case Signal::status:
        return processStatus(log, pidFile, def);

    default:
        break;
    }

    // generic signal handling
    try {
        if (!pidfile::signal(pidFile, def.signo)) {
            // not running -> return 1
            return 1;
        }
    } catch (const std::exception &e) {
        LOG(fatal, log)
            << "Cannot signal running instance: <" << e.what() << ">.";
        return 3;
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
bool daemonizeFinishRun(false);

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
    daemonizeFinishRun = true;
}

extern "C" {
    void service_atfork(void) {
        LOG(info4) << "service_atfork";
        if (!daemonizeFinishRun) {
            daemonizeFinish();
        }
    }
}

} // namespace

void Service::preConfigHook(const po::variables_map &vars)
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

int Service::operator()(int argc, char *argv[])
{
    dbglog::thread_id("main");

    bool daemonize(false);

    Config config;
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
             , "Signal to be sent to running instance: "
             "stop, logrotate, status. "
             "Signal 'stop' can be followed by /timeout specifying number "
             "of seconds to wait for running process to terminate.")
            ;

        config.configuration(genericCmdline, genericConfig);

        auto vm(Program::configure(argc, argv, genericCmdline, genericConfig));
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
            if (-1 == ::pthread_atfork(nullptr, nullptr, &service_atfork)) {
                LOG(fatal, log_)
                    << "Atfork registration failed: " << errno;
                _exit(EXIT_FAILURE);
            }
        }

        LOG(info4, log_) << "Running in background.";
    }

    boost::optional<fs::path> ctrlPath;

    if (!pidFilePath.empty()) {
        // handle pidfile
        try {
            pidfile::allocate(pidFilePath);
        } catch (const std::exception &e) {
            LOG(fatal, log_) << "Cannot allocate pid file: " << e.what();
            return EXIT_FAILURE;
        }
        // control socket path (constructed from pid file)
        ctrlPath = utility::addExtension(pidFilePath, ".ctrl");

        // we need to remove file if exists
        remove_all(*ctrlPath);
        LOG(info4) << "Using control socket at " << *ctrlPath << ".";

        // debug
        ctrlPath = boost::none;
    }

    prePersonaSwitch();
    try {
        switchPersona(log_, config);
    } catch (const std::exception &e) {
        return EXIT_FAILURE;
    }
    postPersonaSwitch();

    // start signal handler in main process
    signalHandler_ = std::make_shared<detail::SignalHandler>
        (log_, *this, ::getpid(), ctrlPath);

    // we are the one that terminates whole daemon!
    globalTerminate(true);

    int code = EXIT_SUCCESS;
    {

        detail::SignalHandler::ScopedHandler signals(*signalHandler_);

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

void Service::stop()
{
    signalHandler_->terminate();
}

bool Service::isRunning() {
    return !signalHandler_->process();
}

void Service::globalTerminate(bool value, long pid)
{
    signalHandler_->globalTerminate(value, pid);
}

void Service::configure(const std::vector<std::string> &)
{
    throw po::error
        ("Program asked to collect unrecognized options "
         "although it is not processing them. Go fix your program.");
}

bool Service::help(std::ostream &, const std::string &)
{
    return false;
}

void Service::processCtrl(const CtrlCommand &cmd, std::ostream &output)
{
    // service supported commands
    if (cmd.cmd == "stat") {
        stat(output);
    } else if (cmd.cmd == "monitor") {
        processMonitor(output);
    } else if (!ctrl(cmd, output)) {
        output << "command <" << cmd.cmd << "> not implemented";
    }
}

bool Service::ctrl(const CtrlCommand &cmd, std::ostream &output)
{
    (void) cmd;
    (void) output;

    return false;
}

void Service::processStat()
{
    std::ostringstream os;
    stat(os);
    LOG(info4) << Program::identity() << " statistics:\n" << os.str();
}

void Service::stat(std::ostream &output)
{
    (void) output;
}

void Service::processMonitor(std::ostream &output)
{
    output
        << "identity: " << Program::versionInfo()
        << "\npid: " << ::getpid()
        << "\n";
    monitor(output);
}

void Service::monitor(std::ostream &output)
{
    (void) output;
}

void Service::Config::configuration(po::options_description &cmdline
                                    , po::options_description &config)
{
    (void) cmdline;
    config.add_options()
        ("service.user", po::value(&username)
         , "Switch process persona to given username.")
        ("service.group", po::value(&groupname)
         , "Switch process persona to given group name.")
        ;
}

void Service::Config::configure(const po::variables_map &vars)
{
    (void) vars;
}

} // namespace Service
