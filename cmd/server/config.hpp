#pragma once
#include <iostream>
#include <string>
#include <cstdlib>

struct Config {
    std::string db_pass;
    std::string minio_ak;
    std::string minio_sk;
    std::string jwt_secret;

    Config() {
        db_pass    = get_env_or_exit("DB_PASSWORD");
        minio_ak   = get_env_or_exit("MINIO_ACCESS_KEY");
        minio_sk   = get_env_or_exit("MINIO_SECRET_KEY");
        jwt_secret = get_env_or_exit("JWT_SECRET");
    }

private:
    std::string get_env_or_exit(const char* key) {
        const char* val = std::getenv(key);
        if (!val) {
            std::cerr << "[Security Error] Missing required environment variable: " << key << std::endl;
            exit(1);
        }
        return std::string(val);
    }
};
