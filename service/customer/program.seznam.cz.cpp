#include "../program.hpp"

namespace service {

// Default copyright.
std::string Program::copyright() const
{
    return R"RAW(Copyright (C) 2011-2016 Melown Technologies SE
Strakonicka 1199/2d, 150 00 Praha 5, Czech Republic)RAW";
}

// Default licence.
std::string Program::licence() const
{
    return R"RAW(
Scope of the licence is subject to the licence agreement.

This is a proprietary software. To be used only with valid licence.
Not to be redistributed.)RAW";
}

// Default licence.
std::string Program::licensee() const
{
    return "Seznam.cz, a.s";
}

// Default licence check.
void Program::licenceCheck() const {}

} // namespace service
