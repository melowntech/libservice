/**
 * Copyright (c) 2017 Melown Technologies SE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * *  Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <cstdint>
#include <memory>
#include <atomic>
#include <system_error>
#include <tuple>
#include <cstring>
#include <iomanip>

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
#include "utility/time.hpp"
#include "utility/path.hpp"
#include "utility/environment.hpp"

namespace fs = boost::filesystem;

namespace service {

Service::Service(const std::string &name, const std::string &version
                 , int flags)
    : Program(name, version, flags)
    , daemonize_(false)
{}

Service::~Service()
{}

namespace {

Persona switchPersona(dbglog::module &log, const Service::Config &config
                      , bool privilegesRegainable)
{
    const auto &username(config.username);
    const auto &groupname(config.groupname);

    // choose proper uid/gid setter
    auto& uidSetter(privilegesRegainable ? ::seteuid : ::setuid);
    auto& gidSetter(privilegesRegainable ? ::setegid : ::setgid);

    bool switchUid(false);
    bool switchGid(false);
    Persona persona;
    persona.start.loadEffectivePersona();
    persona.running = persona.start;

    if (username.empty() && groupname.empty()) { return persona; }
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
        persona.running.uid = pwd->pw_uid;
        persona.running.gid = pwd->pw_gid;
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
        persona.running.gid = gr->gr_gid;
        switchGid = true;
    }

    // change log file owner to uid/gid before persona change
    dbglog::log_file_owner(persona.running.uid, persona.running.gid);

    // TODO: check whether we do not run under root!

    if (switchGid) {
        LOG(info3, log) << "Switching to gid <" << persona.running.gid << ">.";
        if (-1 == gidSetter(persona.running.gid)) {
            std::system_error e(errno, std::system_category());
            LOG(fatal, log)
                << "Cannot switch to gid <" << persona.running.gid << ">: "
                << "<" << e.code() << ", " << e.what() << ">.";
            throw e;
        }
    }

    if (switchUid) {
        LOG(info3, log)
            << "Setting supplementary groups for user <"
            << username << ">.";
        if (-1 == ::initgroups(username.c_str(), persona.running.gid)) {
            std::system_error e(errno, std::system_category());
            LOG(fatal, log)
                << "Cannot initialize supplementary groups for user <"
                << username << ">: <" << e.code()
                << ", " << e.what() << ">.";
            throw e;
        }

        LOG(info3, log) << "Switching to uid <" << persona.running.uid << ">.";
        if (-1 == uidSetter(persona.running.uid)) {
            std::system_error e(errno, std::system_category());
            LOG(fatal, log)
                << "Cannot switch to uid <" << persona.running.uid << ">: "
                << "<" << e.code() << ", " << e.what() << ">.";
            throw e;
        }
    }

    LOG(info3, log)
        << "Run under " << username << ":" << groupname << ".";

    return persona;
}

void loginEnv(const Service::Config &config, const Persona &persona)
{
    if (!config.loginEnv) { return; }

    // not thread safe but there should be no other thread running now
    auto passwd(::getpwuid(persona.running.uid));
    if (!passwd) {
        LOGTHROW(err3, std::runtime_error)
            << "Unable to find passwd entry for uid " << persona.running.uid
            << ".";
        throw;
    }

    utility::Environment env;
    env["LOGNAME"] = env["USER"] = passwd->pw_name;
    env["HOME"] = passwd->pw_dir;
    env["SHELL"] = passwd->pw_shell;
    utility::apply(env);
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
        LOG(info1) << "service_atfork";
        if (!daemonizeFinishRun) {
            daemonizeFinish();
        }
    }
}

boost::optional<detail::CtrlConfig> optional(const detail::CtrlConfig &cc)
{
    if (cc.path.empty()) { return boost::none; }
    return cc;
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

    detail::CtrlConfig ctrlConfig;

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

        ctrlConfig.configuration(genericCmdline, genericConfig);
        config.configuration(genericCmdline, genericConfig);

        auto vm(Program::configure(argc, argv, genericCmdline, genericConfig));
        ctrlConfig.configure(vm);
        config.configure(vm);

        daemonize_ = daemonize = vm.count("daemonize");
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
        } else if (!ctrlConfig.path.empty()) {
            LOG(fatal, log_)
                << "Specified ctrl path without pid file.";
            return EXIT_FAILURE;
        }

        if (!ctrlConfig.path.empty()) {
            ctrlConfig.path = absolute(ctrlConfig.path);
        }
    } catch (const immediate_exit &e) {
        return e.code;
    }

    LOG(info4, log_) << "Service " << identity() << " starting.";

    // daemonize if asked to do so

    // notify that we are (possibly) about to daemonize
    preDaemonize(daemonize);

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

    if (!pidFilePath.empty()) {
        // handle pidfile
        try {
            pidfile::allocate(pidFilePath);
        } catch (const std::exception &e) {
            LOG(fatal, log_) << "Cannot allocate pid file: " << e.what();
            return EXIT_FAILURE;
        }

        if (!ctrlConfig.path.empty()) {
            // we need to remove file if exists
            remove_all(ctrlConfig.path);
            LOG(info4, log_)
                << "Using control socket at " << ctrlConfig.path << ".";
        }
    } else {
        if (!ctrlConfig.path.empty()) {
            LOG(warn4, log_)
                << "Option --ctrl makes sense only together with --pidfile.";
        }
    }

    // start signal handler in main process (before persona switch because of
    // socket)
    signalHandler_ = std::make_shared<detail::SignalHandler>
        (log_, *this, ::getpid(), optional(ctrlConfig));

    {
        auto privilegesRegainable(prePersonaSwitch());
        try {
            persona_ = switchPersona(log_, config, privilegesRegainable);
            loginEnv(config, persona_.value());
        } catch (const std::exception &e) {
            return EXIT_FAILURE;
        }
        postPersonaSwitch();
    }

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
        LOG(info4, log_) << "Normal shutdown.";
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

void Service::configure(const std::vector<std::string>&)
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
    if (cmd.cmd == "help") {
        output
            << "stat           shows service statistics\n"
            << "monitor        returns information suitable for service "
            "monitoring\n"
            ;

        // let child class to append its own help
        ctrl(cmd, output);
    } else if (cmd.cmd == "stat") {
        stat(output);
    } else if (cmd.cmd == "monitor") {
        processMonitor(output);
    } else if (!ctrl(cmd, output)) {
        output << "error: command <" << cmd.cmd << "> not implemented\n";
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
    output << "Service provides no statistics.\n";
}

namespace {

typedef std::vector< ::gid_t> GidList;

GidList getSupplementaryGroups()
{
    auto size(::getgroups(0, nullptr));
    if (size == -1) {
        std::system_error e(errno, std::system_category());
        LOG(err3)
            << "Cannot determine supplemetary groups of process: "
            << "<" << e.code() << ", " << e.what() << ">.";
        throw e;
    }

    GidList list;
    list.resize(size);
    auto nsize(::getgroups(size, list.data()));

    if (nsize == -1) {
        std::system_error e(errno, std::system_category());
        LOG(err3)
            << "Cannot determine supplemetary groups of process: "
            << "<" << e.code() << ", " << e.what() << ">.";
        throw e;
    }
    if (nsize < size) {
        // limit
        list.resize(nsize);
    }

    return list;
}

void printSupplementaryGroups(std::ostream &os)
{
    try {
        auto first(true);
        for (auto gid : getSupplementaryGroups()) {
            if (first) { first = false; } else { os << ' '; }
            os << gid;
        }
    } catch (const std::exception &e) {
        os << "?";
    }
}

} // namespace

void Service::processMonitor(std::ostream &output)
{
    auto uptime(Program::uptime());

    output
        << "Identity: " << Program::versionInfo()
        << "\nName: " << Program::name
        << "\nVersion: " << Program::version
        << "\nPid: " << ::getpid() << " (" << ::getppid() << ")"
        << "\nPersona: " << ::getuid() << " " << ::getgid() << " (";
    printSupplementaryGroups(output);

    output
        << ")"
        << "\nUp-Since: " << utility::formatDateTime(Program::upSince())
        << " (" << utility::formatDateTime(Program::upSince(), true) << " GMT)"
        << "\nUptime: " << uptime.count() << ' '
        << utility::formatDuration(uptime)
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
        ("service.loginEnv", po::value(&loginEnv)->default_value(loginEnv)
         , "Generate login-like environment variables (HOME, USER, ...).")
        ;
}

void Service::Config::configure(const po::variables_map &vars)
{
    (void) vars;
}

void Service::logRotate()
{
    const auto lf(logFile());
    LOG(info3, log_) << "Logrotate: <" << lf << ">.";
    dbglog::log_file(lf.string());
    LOG(info4, log_)
        << "Service " << name << '-' << version << ": log rotated.";
    logRotated(lf);
}

void Service::logRotated(const boost::filesystem::path&) {}

void Service::registerSignal(int signo)
{
    signalHandler_->registerSignal(signo);
}

void Service::signal(int signo)
{
    LOG(warn3) << "You've registered custom signal handling for signal <"
               << signo << "> but forgot to implement a signal handler.";
}

} // namespace Service
