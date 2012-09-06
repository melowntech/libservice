#include <errno.h>

#include <unistd.h>

#include <service/service.hpp>

namespace service {

int service::operator()(int argc, char *argv[])
{
    try {
        configure(argc, argv);
    } catch (const immediate_exit &e) {
        return e.code;
    } catch (const std::exception &e) {
        LOG(fatal, log_module_) << "configure failed: " << e.what();
        return EXIT_FAILURE;
    }

    LOG(info4, log_module_) << "starting";
    start();
    LOG(info4, log_module_) << "started";

    if (daemonize_) {
        if (-1 == daemon(false, false)) {
            LOG(fatal) << "Failed to daemonize: " << errno;
            return EXIT_FAILURE;
        }
    }

    int code(run());
    if (code) {
        LOG(err4, log_module_) << "terminated with error " << code;
    } else {
        LOG(info4, log_module_) << "stopped";
    }

    return code;
}

void service::configure(int argc, char *argv[])
{
    po::options_description cmdline("");
    po::options_description config("");
    configuration(cmdline, config);

    po::options_description all(name);

    po::options_description genericCmdline("command line options");
    genericCmdline.add_options()
        ("help", "produce help message")
        ("version,v", "display version and terminate")
        ("logmask", po::value<dbglog::mask>()
         ->default_value(dbglog::mask(dbglog::get_mask()))
         , "set dbglog logging mask")
        ("config,f", po::value<std::string>()
         , "path to configuration file")
        ("help-all", "show help for both command line and config file")
        ;

    po::options_description genericConfig("configuration file options");
    genericConfig.add_options()
        ("daemonize", po::value<>(&daemonize_)
         ->default_value(daemonize_)->implicit_value(true)
        , "run in daemon mode")
        ;

    all.add(genericCmdline).add(cmdline).add(genericConfig).add(config);

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, all), vm);

    // update log mask if set
    if (vm.count("logmask")) {
        dbglog::set_mask(vm["logmask"].as<dbglog::mask>());
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

    po::notify(vm);

    configure(vm);
}

} // namespace service
