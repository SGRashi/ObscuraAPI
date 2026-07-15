#include <string>
#include <iostream>
#include <jwt-cpp/jwt.h>

namespace Auth {
    inline int get_auth(const std::string& request, const std::string& jwt_secret) {
        size_t auth_pos = request.find("Authorization: Bearer ");
        if(auth_pos == std::string::npos)
        return -1;

        size_t token_start = auth_pos + 22;
        size_t end_pos = request.find("\r\n", token_start);
        if(end_pos == std::string::npos) 
        return -1;

        std::string token = request.substr(token_start, end_pos - token_start);

        try {
            auto decoded = jwt::decode(token);
            auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{jwt_secret})
            .with_issuer("obscura-api")
            .with_type("JWS");

            verifier.verify(decoded);

            if(decoded.has_payload_claim("user_id")) {
                std::string uid_str = decoded.get_payload_claim("user_id").as_string();
                return std::stoi(uid_str);
            }
        }
        catch(const std::exception& e) {
            std::cerr << "JWT validation failed: " << e.what() << "\n";
        }

        return -1;
    }
}
