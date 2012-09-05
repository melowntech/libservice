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
    po::options_description desc(name + ": command line options");
    desc.add_options()
        ("help", "produce help message")
        ("version,v", "display version and terminate")
        ("logmask", po::value<dbglog::mask>()
         ->default_value(dbglog::mask(dbglog::get_mask()))
         , "set dbglog logging mask")
        ("config,f", po::value<std::string>()
         , "path to configuration file")
        ("help-all", "show help for both command line and config file")
        ;

    configuration(desc);

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);

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
        std::cout << desc;
        throw immediate_exit(EXIT_SUCCESS);
    }

    po::notify(vm);

    configure(vm);
}

} // namespace service
