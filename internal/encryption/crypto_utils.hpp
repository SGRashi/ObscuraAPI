#pragma once
#include <sodium.h>
#include <string>
#include <stdexcept>
#include <iostream>

namespace Crypto {

    // Takes a plain text password and returns the Argon2 hash
    inline std::string hash_password(const std::string& plain_password) {
        char hashed_password[crypto_pwhash_STRBYTES];
        
        if (crypto_pwhash_str(
                hashed_password, 
                plain_password.c_str(), 
                plain_password.length(),
                crypto_pwhash_OPSLIMIT_INTERACTIVE, 
                crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
            throw std::runtime_error("Failed to hash password.");
        }
        
        return std::string(hashed_password);
    }

    // Compares a stored hash with a plain text password attempt
    inline bool verify_password(const std::string& stored_hash, const std::string& login_password) {
        // Returns true if they match, false otherwise
        return crypto_pwhash_str_verify(
                stored_hash.c_str(), 
                login_password.c_str(), 
                login_password.length()) == 0;
    }
}
