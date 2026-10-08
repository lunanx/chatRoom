#pragma once

#include <string>

class UserRepository
{
public:
    static bool findByUsername(std::string username);
};
