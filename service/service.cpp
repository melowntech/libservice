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
    po::options_description desc("Options");
    desc.add_options()
        ("help", "produce help message")
        ;

    configuration(desc);

    po::variables_map vars;
    po::store(po::parse_command_line(argc, argv, desc), vars);
    po::notify(vars);

    configure(vars);
}

} // namespace service
