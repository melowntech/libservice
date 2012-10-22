/**
 * @file gconfig.hpp
 * @author Ondrej Prochazka <ondrej.prochazka@citationtech.net>
 *
 * Generic configuration file object.
 */

#ifndef SERVICE_CONFIG_HPP
#define SERVICE_CONFIG_HPP

#include <fstream>
#include <dbglog/dbglog.hpp>

#include <boost/program_options.hpp>

namespace po = boost::program_options;

namespace service {

struct Config {

    typedef std::runtime_error BadInit_t;

    Config() {}
    virtual ~Config() {}

    void init( int argc, char ** argv ) {
        // clear variables map
        vm_ = po::variables_map();

        std::string configFilePath;

        // specify options
        po::options_description generic("Generic options");
        generic.add_options()
            ( "help,h", "usage info" )
            ( "config,f", po::value<std::string>( & configFilePath ),
                "path to configuration file" );

        po::options_description config("Configuration file options");

        addOptions( config );

        
        po::options_description hidden("Hidden options");
        hidden.add_options()
            ( "positional", po::value<std::vector<std::string> >(),
            "positional" );

        // group options
        po::options_description cmdopts, cfgopts;

        cmdopts.add( generic ).add( config ).add( hidden );
        cfgopts.add( config );
        visible.add( generic ).add( config );

        // positional catch-all options
        po::positional_options_description p;
        p.add( "positional", -1 );

        // first, store command line options
        try {

            po::store( po::command_line_parser( argc, argv )
                .options( cmdopts ).positional( p ).run(), vm_ );

        } catch ( boost::program_options::error & e ) {

            LOG( err2 ) << e.what();
            throw BadInit_t( e.what() );
        }

        // help requested?
        if ( ! vm_["help"].empty() ) {
            helpOnly_ = true;
            return;
        }

        helpOnly_ = false;

        // store configuration file options
        if ( ! vm_["config"].empty() ) {
            std::ifstream f;
            f.exceptions( std::ifstream::failbit | std::ifstream::badbit );

            try {

                f.open( vm_[ "config" ].as<std::string>().c_str() );
                f.exceptions( std::ifstream::badbit );
                store( parse_config_file( f, cfgopts ), vm_ );
                f.close();

            } catch( std::ios_base::failure ) {
                throw BadInit_t( "Error reading config file '" +
                    vm_["config"].as<std::string>() + "'." );
            }


        }

        // notify options
        try {
            po::notify( vm_ );
        } catch ( boost::program_options::error & e ) {
            LOG( err2 ) << e.what();
            throw BadInit_t( e.what() );
        }

        fillPositional();

    }

    bool helpOnly() const { return helpOnly_; }

    std::vector<std::string> args() const { return args_; }

    template <class T>
    T getPositional( int i ) const {

        if ( i > (int) args_.size() - 1 )
            LOGTHROW( err2, BadInit_t )
                << "Positional argument " << i << " missing.";
            
        std::istringstream str( args_[i] );

        T value;
        str >> value;
        return value;
    }
    
    void usage( char ** argv = 0x0 ) const {

        if ( argv != 0x0 ) {
            std::cout << "Usage:\n";
            std::cout << "\t" << argv[0] << " [<options>]\n\n";
        }

        std::cout << visible << "\n";
    }

    const po::variables_map& vm() const { return vm_; };

protected :

    virtual void addOptions( po::options_description & optionsdesc ) = 0;

    virtual void fillPositional() {

        // fill positional options
        try {
            args_ = vm_["positional"].as<std::vector<std::string > >();
        } catch ( boost::bad_any_cast & ) {
            // well, let's just say it is empty (it probably is)
            args_ = std::vector<std::string>();
        }
    }

    std::vector<std::string> args_;

    template < typename T >
    po::typed_value<T>* make_value( T* storeTo ) {
        return po::value<T>( storeTo );
    }

    
private:
    po::variables_map vm_;

    bool helpOnly_;
    po::options_description visible;

};

} // namespace service

#endif // SERVICE_CONFIG_HPP
