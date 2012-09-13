#include <errno.h>
#include <unistd.h>

#include <iostream>
#include <fstream>

#include <boost/asio.hpp>

#include <service/service.hpp>

namespace asio = boost::asio;

namespace service {

struct service::SignalHandler : boost::noncopyable {
public:
    SignalHandler()
        : signals_(ios_, SIGTERM, SIGINT)
        , terminated_(false)
    {}

    struct ScopedHandler {
        ScopedHandler(SignalHandler &h) : h(h) { h.start(); }
        ~ScopedHandler() { h.stop(); }

        SignalHandler &h;
    };

    bool terminated() {
        ios_.poll();
        return terminated_;
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

        LOG(debug) << "SignalHandler received signal: <" << signo << ">.";
        switch (signo) {
        case SIGTERM:
        case SIGINT:
            LOG(info2) << "Terminate signal: <" << signo << ">.";
            terminated_ = true;
            break;
        }
        start();
    }

    asio::io_service ios_;
    asio::signal_set signals_;
    bool terminated_;
};

service::service(const std::string &name, const std::string &version)
    : name(name), version(version)
    , log_(dbglog::make_module(name))
    , daemonize_(false)
    , signalHandler_(new SignalHandler)
{}

service::~service()
{}

int service::operator()(int argc, char *argv[])
{
    dbglog::thread_id("main");

    try {
        configure(argc, argv);
    } catch (const immediate_exit &e) {
        return e.code;
    } catch (const po::error &e) {
        std::cerr << name << ": " << e.what() << std::endl;
        return EXIT_FAILURE;
    } catch (const std::exception &e) {
        LOG(fatal, log_) << "Configure failed: " << e.what();
        return EXIT_FAILURE;
    }

    LOG(info4, log_) << "Service " << name << '-' << version << " starting.";

    // daemonize if asked to do so
    if (daemonize_) {
        if (-1 == daemon(false, false)) {
            LOG(fatal) << "Failed to daemonize: " << errno;
            return EXIT_FAILURE;
        }
        dbglog::log_console(false);
        LOG(info4, log_) << "Running in background.";
    }

    int code = EXIT_SUCCESS;
    {
        SignalHandler::ScopedHandler signals(*signalHandler_);

        start();
        LOG(info4, log_) << "Started.";

        code = run();
    }

    if (code) {
        LOG(err4, log_) << "Terminated with error " << code;
    } else {
        LOG(info4, log_) << "Stopped";
    }

    return code;
}

bool service::isRunning() {
    return !signalHandler_->terminated();
}

void service::configure(int argc, char *argv[])
{
    po::options_description cmdline("");
    po::options_description config("");
    configuration(cmdline, config);

    po::options_description genericCmdline("command line options");
    genericCmdline.add_options()
        ("help", "produce help message")
        ("version,v", "display version and terminate")
        ("config,f", po::value<std::string>()
         , "path to configuration file")
        ("help-all", "show help for both command line and config file")
        ;

    po::options_description genericConfig("configuration file options");
    genericConfig.add_options()
        ("log.mask", po::value<dbglog::mask>()
         ->default_value(dbglog::mask(dbglog::get_mask()))
         , "set dbglog logging mask")
        ("log.file", po::value<std::string>()
         , "set dbglog output file (non by default)")
        ("log.console", po::value<bool>()->default_value(true)
         , "enable console logging (always off when daemonized)")

        ("daemonize", po::value(&daemonize_)
         ->default_value(daemonize_)->implicit_value(true)
        , "run in daemon mode")
        ;

    // coll config options
    po::options_description all(name);
    all.add(genericCmdline).add(cmdline)
        .add(genericConfig).add(config);

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, all), vm);

    if (vm.count("config")) {
        const std::string cfg(vm["config"].as<std::string>());
        po::options_description configs(name);
        configs.add(genericConfig).add(config);

        std::ifstream f;
        f.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        try {
            f.open(cfg.c_str());
            f.exceptions(std::ifstream::badbit);
            store(po::parse_config_file(f, configs), vm);
            f.close();

            LOG(info3) << "Loaded configuration from <" << cfg << ">.";
        } catch(std::ios_base::failure) {
            LOG(fatal) << "Cannot read config file <" << cfg << ">.";
            throw immediate_exit(EXIT_FAILURE);
        }
    }

    if (vm.count("version")) {
        std::cout << name << ' ' << version << std::endl;
        throw immediate_exit(EXIT_SUCCESS);
    }

    // check for help
    if (vm.count("help")) {
        std::cout << name << ":\n" << genericCmdline << cmdline;
        throw immediate_exit(EXIT_SUCCESS);
    }

    if (vm.count("help-all")) {
        std::cout << name << ":\n" << genericCmdline << cmdline
                  << '\n' << genericConfig << config;
        throw immediate_exit(EXIT_SUCCESS);
    }

    // update log mask if set
    if (vm.count("log.mask")) {
        dbglog::set_mask(vm["log.mask"].as<dbglog::mask>());
    }

    // set log file if set
    if (vm.count("log.file")) {
        dbglog::log_file(vm["log.file"].as<std::string>());
    }

    // enable/disable log console if set
    if (vm.count("log.console")) {
        dbglog::log_console(vm["log.console"].as<bool>());
    }

    po::notify(vm);

    configure(vm);
}

} // namespace service
