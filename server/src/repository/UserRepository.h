#pragma once

#include <string>

class UserRepository
{
public:
    static bool findByUsername(const std::string& username);
};
